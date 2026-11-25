// Tests for the GPS simulator's drive (tools/gps_sim_route.h): the route and its phases, the drift when standing,
// the tunnel, speeding up the distance, and the file it writes, read back by vtweather's own reader. The last part
// runs the whole drive through the reader's states and the Car, to see the arrow and the held position at work.
#include <atomic>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "check.h"
#include "src/car.h"
#include "src/gps_feed.h"
#include "tools/gps_sim_route.h"

using namespace vtw;
using namespace vtw::sim;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

double metres_between(double lat1, double lon1, double lat2, double lon2) {
    const double north = (lat2 - lat1) * 111320.0;
    const double east = (lon2 - lon1) * 111320.0 * std::cos(lat1 * PI / 180.0);
    return std::sqrt(north * north + east * east);
}

std::vector<Sample> run(DriveSim& sim, int steps, double dt = 1.0) {
    std::vector<Sample> out;
    for (int i = 0; i < steps; i++) out.push_back(sim.step(dt));
    return out;
}

bool is(const Sample& s, const char* phase) { return std::string(s.phase) == phase; }

}  // namespace

int main() {
    const double LAT = 31.8, LON = -92.0;

    // ---- the start: no fix, at the place given, and not for long
    {
        DriveSim sim(LAT, LON, 90, 1.0, 1, true, 1000.0);
        const std::vector<Sample> s = run(sim, 8);
        for (int i = 0; i < 4; i++) {
            CHECK(is(s[(size_t)i], "no fix yet (just switched on)"));
            CHECK(s[(size_t)i].write && !s[(size_t)i].fix && !s[(size_t)i].heading_valid);
            CHECK(s[(size_t)i].satellites == 3 && s[(size_t)i].speed_kmph == 0);
            CHECK_NEAR(s[(size_t)i].lat, LAT, 1e-12);
            CHECK_NEAR(s[(size_t)i].lon, LON, 1e-12);
            CHECK_NEAR(s[(size_t)i].gps_time_unix, 1000.0 + i + 1, 1e-9);
        }
        CHECK(s[4].fix && is(s[4], "pulling away"));
        CHECK(s[4].satellites >= 7 && s[4].hdop < 3.0);
    }

    // ---- a lap: its length, no second cold start, the phases in order, the speeds
    {
        DriveSim sim(LAT, LON, 90, 1.0, 2, true, 0.0);
        CHECK_NEAR(sim.lap_seconds(), 165.0, 1e-9);
        const std::vector<Sample> s = run(sim, 4 + 165 * 3 + 20);
        int nofix = 0;
        for (const Sample& x : s) if (!x.fix && x.write) nofix++;      // (the tunnel has no fix either, but writes nothing)
        CHECK(nofix == 4);                                   // only the very first seconds, never again
        CHECK(sim.lap() == 3);

        double previous = 0;
        for (size_t i = 4; i < s.size(); i++) {
            const double mph = s[i].speed_kmph * 0.621371;
            CHECK(mph >= 0 && mph <= 56.0);
            const bool standing = std::string(s[i].phase) == "stopped at the light" || std::string(s[i].phase) == "creeping along";
            if (!standing && i > 4 && !(std::string(s[i - 1].phase) == "stopped at the light" || std::string(s[i - 1].phase) == "creeping along"))
                CHECK(std::fabs(mph - previous) <= 7.5);     // nothing jumps: at most about 7 mph a second (slowing for a corner)
            previous = mph;
            CHECK(std::isfinite(s[i].lat) && std::isfinite(s[i].lon) && s[i].heading_deg >= 0 && s[i].heading_deg < 360);
            CHECK(s[i].altitude_m > 100 && s[i].altitude_m < 140);
            CHECK(s[i].heading_valid == (s[i].fix && s[i].speed_kmph >= 2.0));          // rasfled2's rule: a direction only above 2 km/h
        }

        // the corners: the car turns right by 90 degrees each time, so after each one it is facing the next way
        std::vector<double> after_corner;
        for (size_t i = 5; i < s.size() && after_corner.size() < 8; i++)
            if (is(s[i], "city street") && !is(s[i - 1], "city street")) after_corner.push_back(s[i].heading_deg);
            else if (is(s[i], "back up to speed") && !is(s[i - 1], "back up to speed")) after_corner.push_back(s[i].heading_deg);
        CHECK(after_corner.size() >= 4);
        CHECK_NEAR(after_corner[0], 180.0, 2.0);             // east, then south
        CHECK_NEAR(after_corner[1], 270.0, 2.0);             // then west
        CHECK_NEAR(after_corner[2], 0.0, 2.0);               // then north (360)
        CHECK_NEAR(after_corner[3], 180.0, 2.0);             // and round again
    }

    // ---- the same seed, the same drive; the true positions do not depend on the seed at all
    {
        DriveSim a(LAT, LON, 90, 1.0, 7, true, 0.0), b(LAT, LON, 90, 1.0, 7, true, 0.0), c(LAT, LON, 90, 1.0, 8, true, 0.0);
        const std::vector<Sample> sa = run(a, 300), sb = run(b, 300), sc = run(c, 300);
        bool different_reported = false;
        for (size_t i = 0; i < sa.size(); i++) {
            CHECK(sa[i].lat == sb[i].lat && sa[i].lon == sb[i].lon && sa[i].speed_kmph == sb[i].speed_kmph && sa[i].heading_deg == sb[i].heading_deg);
            CHECK(sa[i].true_lat == sc[i].true_lat && sa[i].true_lon == sc[i].true_lon);
            if (sa[i].lat != sc[i].lat) different_reported = true;
        }
        CHECK(different_reported);                            // but what the GPS reports while standing differs
    }

    // ---- the distance covered: the speed times the time, however the sim is stepped; warp speeds it up
    for (double warp : {1.0, 10.0}) {
        DriveSim sim(LAT, LON, 90, warp, 3, true, 0.0);
        const std::vector<Sample> s = run(sim, 120);
        int checked = 0;
        for (size_t i = 5; i < s.size(); i++) {
            const bool moving_a = s[i].speed_kmph > 10 && s[i - 1].speed_kmph > 10;
            const bool plain = !is(s[i], "stopped at the light") && !is(s[i - 1], "stopped at the light") && !is(s[i], "creeping along") && !is(s[i - 1], "creeping along");
            if (!moving_a || !plain) continue;
            const double metres = metres_between(s[i - 1].true_lat, s[i - 1].true_lon, s[i].true_lat, s[i].true_lon);
            const double expected = (s[i].speed_kmph + s[i - 1].speed_kmph) / 2.0 / 3.6 * warp;
            CHECK_NEAR(metres, expected, 0.01 * expected + 0.2);
            checked++;
        }
        CHECK(checked > 40);
    }
    {
        DriveSim slow(LAT, LON, 90, 1.0, 3, true, 0.0), fast(LAT, LON, 90, 10.0, 3, true, 0.0);
        const std::vector<Sample> a = run(slow, 20), b = run(fast, 20);
        CHECK(a[15].speed_kmph == b[15].speed_kmph);          // the written speed is the real one
        CHECK(metres_between(LAT, LON, b[15].true_lat, b[15].true_lon) > 8.0 * metres_between(LAT, LON, a[15].true_lat, a[15].true_lon));
    }
    {   // a different step size gives the same road
        DriveSim one(LAT, LON, 90, 1.0, 4, true, 0.0), half(LAT, LON, 90, 1.0, 4, true, 0.0);
        const std::vector<Sample> a = run(one, 30, 1.0), b = run(half, 60, 0.5);
        CHECK(metres_between(a[29].true_lat, a[29].true_lon, b[59].true_lat, b[59].true_lon) < 5.0);
    }

    // ---- standing at the light: the true position stays, the reported one wanders; the speed jitters and the heading is noise
    {
        DriveSim sim(LAT, LON, 90, 1.0, 5, true, 0.0);
        const std::vector<Sample> s = run(sim, 120);
        std::vector<Sample> standing;
        for (const Sample& x : s) if (is(x, "stopped at the light")) standing.push_back(x);
        CHECK(standing.size() == 12);
        double widest = 0;
        for (size_t i = 1; i < standing.size(); i++) {
            CHECK(metres_between(standing[i - 1].true_lat, standing[i - 1].true_lon, standing[i].true_lat, standing[i].true_lon) < 0.01);
            const double off = metres_between(standing[i].true_lat, standing[i].true_lon, standing[i].lat, standing[i].lon);
            widest = std::max(widest, off);
            CHECK(off < 40.0);
            CHECK(standing[i].speed_kmph < 6.0);
            CHECK(standing[i].fix && standing[i].write);
        }
        CHECK(widest > 1.0);                                  // it does wander
        bool varies = false;
        for (size_t i = 1; i < standing.size(); i++) if (standing[i].lat != standing[i - 1].lat) varies = true;
        CHECK(varies);
    }

    // ---- the tunnel: nothing written for the 8 seconds, once a lap; without it, no gap
    {
        DriveSim sim(LAT, LON, 90, 1.0, 6, true, 0.0);
        const std::vector<Sample> s = run(sim, 4 + 165 * 2);
        int silent = 0, runs = 0;
        for (size_t i = 0; i < s.size(); i++) {
            if (!s[i].write) { silent++; if (i == 0 || s[i - 1].write) runs++; CHECK(is(s[i], "tunnel: no GPS")); CHECK(s[i].fix && s[i].speed_kmph > 80); }   // the car drives on
            else CHECK(!is(s[i], "tunnel: no GPS"));
        }
        CHECK(silent == 16 && runs == 2);
        DriveSim plain(LAT, LON, 90, 1.0, 6, false, 0.0);
        for (const Sample& x : run(plain, 4 + 165 * 2)) CHECK(x.write);
    }

    // ---- the file it writes, read back by vtweather's reader
    {
        const std::string dir = std::string(VTW_TEST_DATA_DIR) + "/../gps_sim_scratch";
        const std::string path = dir + "/sub/gps.json";
        const std::string rm = "rm -rf " + dir;
        if (std::system(rm.c_str()) != 0) std::printf("note: could not clear %s\n", dir.c_str());
        mkdir(dir.c_str(), 0755);

        Sample s;
        s.fix = true;
        s.lat = 31.81234567;
        s.lon = -91.98765432;
        s.speed_kmph = 96.04;
        s.heading_deg = 359.97;                              // rounds to 360: must be written as 0
        s.heading_valid = true;
        s.altitude_m = 125.5;
        s.satellites = 10;
        s.hdop = 1.34;
        s.gps_time_unix = 1764079402.0;
        CHECK(write_gps_file(path, s, 41, 1764079403.5));
        GpsFix fix;
        std::string problem;
        CHECK(parse_gps_json(slurp(path), fix, problem));
        CHECK(fix.fix && fix.heading_valid);
        CHECK_NEAR(fix.lat, 31.8123457, 1e-7);
        CHECK_NEAR(fix.lon, -91.9876543, 1e-7);
        CHECK_NEAR(fix.speed_kmph, 96.0, 1e-9);
        CHECK(fix.heading_deg == 0.0);                         // 359.97 would round to 360.0, which is not a heading
        CHECK(fix.satellites == 10 && fix.seq == 41);
        CHECK_NEAR(fix.hdop, 1.3, 1e-9);
        CHECK_NEAR(fix.altitude_m, 125.5, 1e-9);
        CHECK_NEAR(fix.gps_time_unix, 1764079402.0, 1e-6);
        CHECK_NEAR(fix.written_unix, 1764079403.5, 1e-6);
        CHECK(access((path + ".tmp").c_str(), F_OK) != 0);     // no temporary file left behind

        // no fix is a file too
        Sample nofix;
        nofix.satellites = 3;
        nofix.hdop = 9.9;
        nofix.lat = LAT;
        nofix.lon = LON;
        CHECK(write_gps_file(path, nofix, 42, 1764079404.0));
        CHECK(parse_gps_json(slurp(path), fix, problem));
        CHECK(!fix.fix && fix.satellites == 3 && fix.seq == 42);

        // exactly the keys the real writer (rasfled2's gps_exchange.h) writes
        const nlohmann::json real = nlohmann::json::parse(slurp(std::string(VTW_TEST_DATA_DIR) + "/gps_real_writer.json"));
        const nlohmann::json mine = nlohmann::json::parse(slurp(path));
        std::set<std::string> real_keys, my_keys;
        for (auto it = real.begin(); it != real.end(); ++it) real_keys.insert(it.key());
        for (auto it = mine.begin(); it != mine.end(); ++it) my_keys.insert(it.key());
        CHECK(real_keys == my_keys);
        CHECK(real_keys.size() == 13);
        for (auto it = real.begin(); it != real.end(); ++it) CHECK(it.value().type() == mine[it.key()].type() || (it.value().is_number() && mine[it.key()].is_number()));

        // an odd value never gets into the file as a non-number
        Sample odd = s;
        odd.lat = std::nan("");
        odd.speed_kmph = INFINITY;
        CHECK(write_gps_file(path, odd, 43, 1764079405.0));
        const std::string text = slurp(path);
        CHECK(text.find("nan") == std::string::npos && text.find("inf") == std::string::npos);

        // not writable: false, nothing left
        CHECK(!write_gps_file("/proc/no_such_place/gps.json", s, 1, 1.0));
        CHECK(!write_gps_file("", s, 1, 1.0));

        // a reader never sees half a file while it is written over and over
        std::atomic<bool> stop(false);
        std::atomic<long> reads(0), bad(0);
        std::thread reader([&]() {
            while (!stop.load()) {
                GpsFix f;
                std::string why;
                const std::string t = slurp(path);
                if (t.empty()) { bad++; continue; }
                reads++;
                if (!parse_gps_json(t, f, why)) bad++;
            }
        });
        for (int i = 0; i < 3000; i++) {
            Sample w = s;
            w.lat = 30.0 + i * 0.001;
            write_gps_file(path, w, (unsigned long long)i, 1.0);
        }
        stop = true;
        reader.join();
        CHECK(reads.load() > 20);
        CHECK(bad.load() == 0);
        if (std::system(rm.c_str()) != 0) std::printf("note: could not clear %s\n", dir.c_str());
    }

    // ---- the whole drive through the reader's states and the car
    {
        DriveSim sim(LAT, LON, 90, 1.0, 9, true, 0.0);
        Car car;
        int silent = 0;
        bool saw_arrow_cruising = false, saw_plus_stopped = false, creep_kept_arrow = false, hidden_in_tunnel = false;
        GeoPoint held{0, 0};
        int standing_seen = 0;
        double widest_drift_vs_held = 0;
        for (int i = 0; i < 4 + 165 * 2; i++) {
            const Sample s = sim.step(1.0);
            GpsFix fix;
            fix.fix = s.fix;
            fix.lat = s.lat;
            fix.lon = s.lon;
            fix.speed_kmph = s.speed_kmph;
            fix.heading_deg = s.heading_deg;
            fix.heading_valid = s.heading_valid;
            fix.satellites = s.satellites;
            GpsState state = s.fix ? GpsState::Fix : GpsState::NoFix;
            if (!s.write) {                                    // the reader says lost after 5 seconds without a file
                silent++;
                state = silent >= 5 ? GpsState::Stale : GpsState::Fix;
                if (silent < 5) continue;                      // a fresh file still: nothing new to give the car
            } else {
                silent = 0;
            }
            car.update(state, fix);

            if (is(s, "cruising")) { if (car.arrow()) saw_arrow_cruising = true; CHECK(car.moving()); }
            if (is(s, "tunnel: no GPS") && silent >= 5) { if (!car.visible()) hidden_in_tunnel = true; }
            if (is(s, "creeping along") && car.arrow()) creep_kept_arrow = true;             // 4 mph, but it was moving: in the gap
            if (is(s, "stopped at the light")) {
                standing_seen++;
                if (standing_seen == 1) held = car.position();
                if (standing_seen >= 2) {                      // standing: a +, and the position is held, though the GPS wanders
                    CHECK(!car.arrow());
                    if (!car.arrow()) saw_plus_stopped = true;
                    CHECK(car.position().lat == held.lat && car.position().lon == held.lon);
                    widest_drift_vs_held = std::max(widest_drift_vs_held, metres_between(held.lat, held.lon, s.lat, s.lon));
                }
            }
            if (!is(s, "stopped at the light")) standing_seen = 0;
        }
        CHECK(saw_arrow_cruising);
        CHECK(saw_plus_stopped);
        CHECK(creep_kept_arrow);
        CHECK(hidden_in_tunnel);
        CHECK(widest_drift_vs_held > 2.0);                     // the GPS did wander while the car's position stayed put
    }

    if (failures == 0) std::printf("all gps_sim tests passed\n");
    return failures == 0 ? 0 : 1;
}
