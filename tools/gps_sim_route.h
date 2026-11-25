// The drive the GPS simulator (gps_sim.cpp) makes up, and the writer of the GPS file. It is here, in a header, so
// the tests can run it without a real clock.
//
// The car drives round a block, over and over, with things a real drive has:
//   - the first seconds with no fix (a GPS just switched on)
//   - pulling away, cruising and turning corners (the heading turns smoothly)
//   - braking for a light and standing still, where a real GPS wanders: the position drifts by several
//     metres, the speed jitters and the direction it reports is noise
//   - a creep at a few mph, between the "stopped" and "moving" speeds
//   - a few seconds in a tunnel, with nothing written at all (the program that writes the file would
//     have nothing to tell)
//
// What is written is what rasfled2's gps_exchange.h writes (see gps_feed.h for the layout), so vtweather
// cannot tell the difference. Distances can be speeded up (warp) so that a drive crosses a zoomed-out map in
// a reasonable time; the speed that is written stays the real one.
#pragma once

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace vtw {
namespace sim {

constexpr double PI = 3.14159265358979323846;

enum class Mode { Fix, NoFix, Silent };

struct Phase {
    const char* name;
    double seconds;
    double speed_end_mph;          // the speed at the end of the phase (from the one the last phase ended with)
    double turn_deg;               // how far the heading turns over the phase, right (clockwise) positive
    Mode mode;
    bool drift;                    // standing about: the reported position wanders
    bool first_lap_only;
};

// One second of the drive, as the GPS would report it.
struct Sample {
    const char* phase = "";
    bool write = true;             // false in the tunnel: nothing is written (the GPS still has a fix, and the car drives on)
    bool fix = false;
    double lat = 0, lon = 0;       // what the GPS reports (drifting when standing about)
    double true_lat = 0, true_lon = 0;
    double speed_kmph = 0;
    double heading_deg = 0;
    bool heading_valid = false;
    double altitude_m = 0;
    int satellites = 0;
    double hdop = 0;
    double gps_time_unix = 0;
};

class DriveSim {
public:
    // start: where the car is. heading: the way it faces (0 = north, 90 = east). warp: how many times the real
    // distance is covered. tunnel: whether there is a dropout.
    DriveSim(double lat, double lon, double heading_deg, double warp, unsigned seed, bool tunnel, double start_unix)
        : lat_(lat), lon_(lon), heading_(heading_deg), warp_(warp), rng_(seed), start_unix_(start_unix) {
        phases_ = {
            {"no fix yet (just switched on)", 4, 0, 0, Mode::NoFix, false, true},
            {"pulling away", 10, 50, 0, Mode::Fix, false, false},
            {"cruising", 25, 50, 0, Mode::Fix, false, false},
            {"right turn", 5, 20, 90, Mode::Fix, false, false},
            {"city street", 12, 35, 0, Mode::Fix, false, false},
            {"braking for a light", 7, 0, 0, Mode::Fix, false, false},
            {"stopped at the light", 12, 0, 0, Mode::Fix, true, false},
            {"pulling away", 8, 35, 0, Mode::Fix, false, false},
            {"right turn", 5, 20, 90, Mode::Fix, false, false},
            {"back up to speed", 10, 55, 0, Mode::Fix, false, false},
            {tunnel ? "tunnel: no GPS" : "cruising", 8, 55, 0, tunnel ? Mode::Silent : Mode::Fix, false, false},
            {"cruising", 14, 55, 0, Mode::Fix, false, false},
            {"right turn", 5, 20, 90, Mode::Fix, false, false},
            {"city street", 15, 30, 0, Mode::Fix, false, false},
            {"slowing to a creep", 8, 4, 0, Mode::Fix, false, false},
            {"creeping along", 8, 4, 0, Mode::Fix, true, false},
            {"pulling away", 8, 30, 0, Mode::Fix, false, false},
            {"right turn", 5, 20, 90, Mode::Fix, false, false},
        };
        phase_ = 0;
    }

    // Move the clock on dt seconds and say what the GPS reports now.
    Sample step(double dt) {
        const Phase& p = phases_[phase_];
        const double before = speed_mph_;
        const double along = p.seconds > 0 ? std::min(1.0, (time_in_phase_ + dt) / p.seconds) : 1.0;
        const double target = phase_start_speed_ + (p.speed_end_mph - phase_start_speed_) * along;
        speed_mph_ = target;
        const double mean_mph = (before + speed_mph_) / 2.0;
        const double heading_change = p.seconds > 0 ? p.turn_deg * dt / p.seconds : 0.0;

        // the true position: only while the car is going somewhere
        if (p.mode != Mode::NoFix) {
            const double metres = mean_mph * 0.44704 * dt * warp_;
            const double h = (heading_ + heading_change / 2.0) * PI / 180.0;
            lat_ += metres * std::cos(h) / 111320.0;
            lon_ += metres * std::sin(h) / (111320.0 * std::cos(lat_ * PI / 180.0));
            heading_ = wrap(heading_ + heading_change);
        }
        elapsed_ += dt;

        Sample s;
        s.phase = p.name;
        s.write = p.mode != Mode::Silent;
        s.fix = p.mode != Mode::NoFix;                   // in the tunnel the GPS still has its fix: it is the writing that stops
        s.true_lat = lat_;
        s.true_lon = lon_;
        s.gps_time_unix = start_unix_ + elapsed_;
        std::normal_distribution<double> unit(0.0, 1.0);
        if (p.drift) {                                   // standing about: a slow wander, a few metres, and noise
            drift_north_ = 0.85 * drift_north_ + 3.0 * unit(rng_);
            drift_east_ = 0.85 * drift_east_ + 3.0 * unit(rng_);
            s.speed_kmph = speed_mph_ / 0.621371 + std::fabs(unit(rng_)) * 0.9;     // the true speed, and a jitter on top
        } else {
            drift_north_ *= 0.5;                         // the wander dies away once moving
            drift_east_ *= 0.5;
            s.speed_kmph = speed_mph_ / 0.621371;
        }
        s.lat = lat_ + drift_north_ / 111320.0;
        s.lon = lon_ + drift_east_ / (111320.0 * std::cos(lat_ * PI / 180.0));
        s.heading_deg = heading_;
        if (s.speed_kmph < 10.0) s.heading_deg = wrap(heading_ + 15.0 * unit(rng_));       // a slow car: the direction is shaky
        if (p.drift && s.speed_kmph < 2.0) s.heading_deg = 360.0 * (0.5 + 0.5 * std::sin(elapsed_ * 7.3));   // standing: noise
        s.heading_valid = s.fix && s.speed_kmph >= 2.0;                                 // rasfled2's rule: over 2 km/h
        s.altitude_m = 120.0 + 6.0 * std::sin(elapsed_ / 20.0);
        s.satellites = s.fix ? 9 + (int)std::lround(1.4 * std::sin(elapsed_ / 9.0)) : 3;
        s.hdop = s.fix ? 1.1 + 0.3 * std::sin(elapsed_ / 13.0) : 9.9;
        if (!s.fix) { s.speed_kmph = 0; s.heading_valid = false; }
        if (!s.fix) { s.lat = lat_; s.lon = lon_; }

        // on to the next phase
        time_in_phase_ += dt;
        if (time_in_phase_ >= p.seconds - 1e-9) {
            time_in_phase_ -= p.seconds;
            phase_start_speed_ = p.speed_end_mph;
            phase_++;
            if (phase_ >= phases_.size()) { phase_ = 0; lap_++; }
            while (lap_ > 0 && phases_[phase_].first_lap_only) phase_++;               // the cold start is only the first time
        }
        return s;
    }

    int lap() const { return lap_; }
    double lap_seconds() const {                       // one lap after the first
        double t = 0;
        for (const Phase& p : phases_) if (!p.first_lap_only) t += p.seconds;
        return t;
    }

private:
    static double wrap(double degrees) {
        double a = std::fmod(degrees, 360.0);
        if (a < 0) a += 360.0;
        return a >= 360.0 ? 0.0 : a;
    }

    std::vector<Phase> phases_;
    size_t phase_ = 0;
    int lap_ = 0;
    double time_in_phase_ = 0, elapsed_ = 0;
    double lat_, lon_, heading_;
    double warp_;
    double speed_mph_ = 0, phase_start_speed_ = 0;
    double drift_north_ = 0, drift_east_ = 0;
    std::mt19937 rng_;
    double start_unix_ = 0;
};

// A number for the file: a dot always, no "nan" or "inf".
inline std::string json_number(double value, int decimals) {
    if (!std::isfinite(value)) value = 0.0;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(decimals) << value;
    return text.str();
}

// Write the sample as the GPS file: to a temporary file beside it, then renamed over (so a reader never sees half a
// file), the way rasfled2 does it. False if it could not be written.
inline bool write_gps_file(const std::string& path, const Sample& s, unsigned long long seq, double written_unix) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << "{\n";
    json << "  \"version\": 1,\n";
    json << "  \"seq\": " << seq << ",\n";
    json << "  \"written_unix\": " << json_number(written_unix, 3) << ",\n";
    json << "  \"gps_time_unix\": " << json_number(s.gps_time_unix, 3) << ",\n";
    json << "  \"fix\": " << (s.fix ? "true" : "false") << ",\n";
    json << "  \"lat\": " << json_number(s.lat, 7) << ",\n";
    json << "  \"lon\": " << json_number(s.lon, 7) << ",\n";
    json << "  \"altitude_m\": " << json_number(s.altitude_m, 1) << ",\n";
    json << "  \"speed_kmph\": " << json_number(s.speed_kmph, 1) << ",\n";
    json << "  \"heading_deg\": " << json_number(std::round(s.heading_deg * 10.0) / 10.0 >= 360.0 ? 0.0 : s.heading_deg, 1) << ",\n";
    json << "  \"heading_valid\": " << (s.heading_valid ? "true" : "false") << ",\n";
    json << "  \"satellites\": " << s.satellites << ",\n";
    json << "  \"hdop\": " << json_number(s.hdop, 1) << "\n";
    json << "}\n";
    const std::string text = json.str();

    const std::string::size_type slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) mkdir(path.substr(0, slash).c_str(), 0755);

    const std::string temp = path + ".tmp";
    std::FILE* file = std::fopen(temp.c_str(), "w");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const int closed = std::fclose(file);
    if (written != text.size() || closed != 0) {
        std::remove(temp.c_str());
        return false;
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

}  // namespace sim
}  // namespace vtw
