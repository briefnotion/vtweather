#include "gps_feed.h"

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace vtw {

namespace {

using Json = nlohmann::json;

// An optional number: left alone if it is missing, not a number or not finite.
void read_number(const Json& object, const char* key, double& out) {
    if (!object.contains(key) || !object.at(key).is_number()) return;
    const double value = object.at(key).get<double>();
    if (std::isfinite(value)) out = value;
}

}  // namespace

bool parse_gps_json(const std::string& text, GpsFix& fix, std::string& problem) {
    const Json json = Json::parse(text, nullptr, false);         // false: no exception
    if (json.is_discarded() || !json.is_object()) {
        problem = "not valid JSON";
        return false;
    }
    if (!json.contains("version") || !json.at("version").is_number_integer()) {
        problem = "no version";
        return false;
    }
    const long long version = json.at("version").get<long long>();
    if (version != GPS_FILE_VERSION) {
        problem = "version " + std::to_string(version) + " is not supported (this reads " + std::to_string(GPS_FILE_VERSION) + ")";
        return false;
    }
    if (!json.contains("fix") || !json.at("fix").is_boolean() ||
        !json.contains("lat") || !json.at("lat").is_number() ||
        !json.contains("lon") || !json.at("lon").is_number()) {
        problem = "fix, lat or lon missing";
        return false;
    }

    GpsFix read;
    read.fix = json.at("fix").get<bool>();
    read.lat = json.at("lat").get<double>();
    read.lon = json.at("lon").get<double>();
    if (!std::isfinite(read.lat) || !std::isfinite(read.lon) || read.lat < -90 || read.lat > 90 ||
        read.lon < -180 || read.lon > 180) {
        problem = "position is not on the earth";
        return false;
    }

    read_number(json, "altitude_m", read.altitude_m);
    read_number(json, "speed_kmph", read.speed_kmph);
    read_number(json, "heading_deg", read.heading_deg);
    read_number(json, "hdop", read.hdop);
    read_number(json, "gps_time_unix", read.gps_time_unix);
    read_number(json, "written_unix", read.written_unix);
    double count = 0, sequence = 0;
    read_number(json, "satellites", count);
    read_number(json, "seq", sequence);
    read.satellites = (int)count;
    read.seq = (long long)sequence;
    if (json.contains("heading_valid") && json.at("heading_valid").is_boolean())
        read.heading_valid = json.at("heading_valid").get<bool>();
    if (read.speed_kmph < 0) read.speed_kmph = 0;
    if (read.hdop < 0) read.hdop = 0;
    if (read.heading_deg < 0 || read.heading_deg >= 360) {       // not a direction: do not use it
        read.heading_deg = 0;
        read.heading_valid = false;
    }

    fix = read;
    problem.clear();
    return true;
}

GpsFeed::GpsFeed(std::string path) : path_(std::move(path)) {
    if (path_.empty()) state_ = GpsState::Off;
}

void GpsFeed::update(double now_unix) {
    if (path_.empty()) return;
    // look at the file about four times a second (a clock that went backwards also counts as time)
    if (!polled_ || now_unix < last_poll_ || now_unix - last_poll_ >= GPS_POLL_SECONDS) {
        polled_ = true;
        last_poll_ = now_unix;
        poll();
    }
    judge(now_unix);                                  // the age moves on even when the file is not looked at
}

void GpsFeed::poll() {
    struct stat info;
    if (::stat(path_.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        file_seen_ = false;
        seen_sec_ = seen_nsec_ = -1;                  // if it comes back, read it again
        return;
    }
    file_seen_ = true;
    mtime_ = (double)info.st_mtim.tv_sec + (double)info.st_mtim.tv_nsec * 1e-9;
    if ((long long)info.st_mtim.tv_sec == seen_sec_ && (long long)info.st_mtim.tv_nsec == seen_nsec_) return;   // nothing new
    seen_sec_ = (long long)info.st_mtim.tv_sec;
    seen_nsec_ = (long long)info.st_mtim.tv_nsec;

    if (info.st_size < 0 || (size_t)info.st_size > GPS_MAX_FILE_BYTES) {
        bad_ = true;
        problem_ = "file is too big";
        return;
    }
    std::ifstream in(path_, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (!in.good() && !in.eof()) {
        bad_ = true;
        problem_ = "file could not be read";
        return;
    }
    GpsFix read;
    std::string problem;
    if (parse_gps_json(text.str(), read, problem)) {
        fix_ = read;
        have_data_ = true;
        bad_ = false;
        problem_.clear();
    } else {
        bad_ = true;
        problem_ = problem;
    }
}

void GpsFeed::judge(double now_unix) {
    if (!file_seen_) { state_ = GpsState::NoFile; return; }
    const double age = now_unix - mtime_;
    if (age > GPS_STALE_SECONDS || age < -GPS_STALE_SECONDS) { state_ = GpsState::Stale; return; }   // old, or the clock was moved
    if (bad_ || !have_data_) { state_ = GpsState::Bad; return; }
    state_ = fix_.fix ? GpsState::Fix : GpsState::NoFix;
}

// Retired: the driving card (cards.h) says this now.
#if 0
std::string gps_status_text(const GpsFeed& gps, double now_unix) {
    char text[160];
    const GpsFix& f = gps.fix();
    switch (gps.state()) {
        case GpsState::Off:
        case GpsState::NoFile:
            return "";
        case GpsState::Bad:
            return "gps: unreadable (" + gps.problem() + ")";
        case GpsState::Stale:
            std::snprintf(text, sizeof text, "gps: lost, nothing for %d s", (int)std::max(0.0, gps.age(now_unix)));
            return text;
        case GpsState::NoFix:
            std::snprintf(text, sizeof text, "gps: no fix yet, %d satellites", f.satellites);
            return text;
        case GpsState::Fix:
            if (f.heading_valid)
                std::snprintf(text, sizeof text, "gps: %.0f km/h, heading %.0f, %d satellites", f.speed_kmph, f.heading_deg, f.satellites);
            else
                std::snprintf(text, sizeof text, "gps: %.0f km/h, %d satellites", f.speed_kmph, f.satellites);
            return text;
    }
    return "";
}
#endif

}  // namespace vtw
