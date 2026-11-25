// Tests for the GPS reader: the file's layout (including what rasfled2's real writer produces), damaged and
// odd files, and the feed's states with a made-up clock and files whose modification times are set by hand.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "src/gps_feed.h"

using namespace vtw;
namespace fs = std::filesystem;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Write a file the way the real writer does (a new file, renamed over), and give it this modification time.
void put(const std::string& path, const std::string& text, double mtime_unix) {
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        out << text;
    }
    timespec times[2];
    times[0].tv_sec = times[1].tv_sec = (time_t)mtime_unix;
    times[0].tv_nsec = times[1].tv_nsec = (long)((mtime_unix - (double)(time_t)mtime_unix) * 1e9);
    CHECK(utimensat(AT_FDCWD, temp.c_str(), times, 0) == 0);
    CHECK(std::rename(temp.c_str(), path.c_str()) == 0);
}

std::string fix_json(double lat, double lon, bool fix = true, double speed = 50, double heading = 90, bool heading_valid = true,
                     int satellites = 8, int seq = 1) {
    char text[600];
    std::snprintf(text, sizeof text,
                  "{\n  \"version\": 1,\n  \"seq\": %d,\n  \"written_unix\": 1.0,\n  \"gps_time_unix\": 2.0,\n  \"fix\": %s,\n"
                  "  \"lat\": %.7f,\n  \"lon\": %.7f,\n  \"altitude_m\": 100.0,\n  \"speed_kmph\": %.1f,\n  \"heading_deg\": %.1f,\n"
                  "  \"heading_valid\": %s,\n  \"satellites\": %d,\n  \"hdop\": 1.5\n}\n",
                  seq, fix ? "true" : "false", lat, lon, speed, heading, heading_valid ? "true" : "false", satellites);
    return text;
}

bool parses(const std::string& text) {
    GpsFix fix;
    std::string problem;
    return parse_gps_json(text, fix, problem);
}

std::string problem_of(const std::string& text) {
    GpsFix fix;
    std::string problem;
    parse_gps_json(text, fix, problem);
    return problem;
}

void parser_tests() {
    const std::string dir = VTW_TEST_DATA_DIR;

    // what the real writer (rasfled2/source/gps_exchange.h) makes
    const std::string real = slurp(dir + "/gps_real_writer.json");
    CHECK(real.size() > 100);
    GpsFix fix;
    std::string problem;
    CHECK(parse_gps_json(real, fix, problem));
    CHECK(problem.empty());
    CHECK(fix.fix);
    CHECK_NEAR(fix.lat, 37.6901235, 1e-9);
    CHECK_NEAR(fix.lon, -97.3409877, 1e-9);
    CHECK_NEAR(fix.altitude_m, 412.0, 1e-9);
    CHECK_NEAR(fix.speed_kmph, 88.6, 1e-9);
    CHECK_NEAR(fix.heading_deg, 87.0, 1e-9);
    CHECK(fix.heading_valid);
    CHECK(fix.satellites == 9);
    CHECK_NEAR(fix.hdop, 1.2, 1e-9);
    CHECK_NEAR(fix.gps_time_unix, 1764079402.0, 1e-6);
    CHECK(fix.written_unix > 1.7e9);
    CHECK(fix.seq == 1);

    GpsFix nofix;
    CHECK(parse_gps_json(slurp(dir + "/gps_real_writer_nofix.json"), nofix, problem));
    CHECK(!nofix.fix);                                   // no fix, with the last known place
    CHECK_NEAR(nofix.lat, 37.6901235, 1e-9);
    CHECK(!nofix.heading_valid && nofix.satellites == 3 && nofix.seq == 2);

    // a damaged file: every cut-off shorter than the whole thing (but the last newline) is refused, never crashes
    for (size_t n = 0; n + 2 < real.size(); n++) {
        GpsFix untouched;
        untouched.lat = 12.5;
        std::string why;
        CHECK(!parse_gps_json(real.substr(0, n), untouched, why));
        CHECK(untouched.lat == 12.5);                    // a refused file leaves the answer alone
        CHECK(!why.empty());
    }
    CHECK(parses(real.substr(0, real.size() - 1)));      // only the final newline missing: still a whole file

    // the small file with only what is needed
    CHECK(parses(R"({"version":1,"fix":true,"lat":1,"lon":2})"));
    GpsFix small;
    parse_gps_json(R"({"version":1,"fix":false,"lat":-33.5,"lon":151.25})", small, problem);
    CHECK(!small.fix && small.lat == -33.5 && small.lon == 151.25 && small.satellites == 0 && small.speed_kmph == 0 && !small.heading_valid);

    // not ours
    CHECK(problem_of("") == "not valid JSON");
    CHECK(problem_of("hello") == "not valid JSON");
    CHECK(problem_of("[1,2,3]") == "not valid JSON");
    CHECK(problem_of("{}") == "no version");
    CHECK(problem_of(R"({"version":"1","fix":true,"lat":1,"lon":2})") == "no version");
    CHECK(problem_of(R"({"version":1.5,"fix":true,"lat":1,"lon":2})") == "no version");
    CHECK(problem_of(R"({"version":2,"fix":true,"lat":1,"lon":2})").find("version 2 is not supported") == 0);
    CHECK(problem_of(R"({"version":0,"fix":true,"lat":1,"lon":2})").find("version 0") == 0);
    CHECK(problem_of(R"({"version":1,"lat":1,"lon":2})") == "fix, lat or lon missing");
    CHECK(problem_of(R"({"version":1,"fix":true,"lon":2})") == "fix, lat or lon missing");
    CHECK(problem_of(R"({"version":1,"fix":true,"lat":1})") == "fix, lat or lon missing");
    CHECK(problem_of(R"({"version":1,"fix":"yes","lat":1,"lon":2})") == "fix, lat or lon missing");
    CHECK(problem_of(R"({"version":1,"fix":true,"lat":"1","lon":2})") == "fix, lat or lon missing");
    CHECK(problem_of(R"({"version":1,"fix":true,"lat":null,"lon":2})") == "fix, lat or lon missing");

    // positions that cannot be on the earth; the edges are fine
    for (const char* bad : {R"("lat":90.0001,"lon":0)", R"("lat":-90.5,"lon":0)", R"("lat":0,"lon":180.01)", R"("lat":0,"lon":-181)",
                            R"("lat":1e999,"lon":0)", R"("lat":0,"lon":-1e999)"}) {
        CHECK(!parses(std::string(R"({"version":1,"fix":true,)") + bad + "}"));
    }
    for (const char* good : {R"("lat":90,"lon":180)", R"("lat":-90,"lon":-180)", R"("lat":0,"lon":0)"}) {
        CHECK(parses(std::string(R"({"version":1,"fix":true,)") + good + "}"));
    }

    // the extra information is forgiving: a wrong type or a nonsense value is not a reason to refuse the position
    GpsFix odd;
    CHECK(parse_gps_json(R"({"version":1,"fix":true,"lat":1,"lon":2,"speed_kmph":"fast","satellites":"many","hdop":-4,"unknown":[1,2],
                             "heading_deg":361,"heading_valid":true,"altitude_m":null,"seq":"x"})", odd, problem));
    CHECK(odd.speed_kmph == 0 && odd.satellites == 0 && odd.hdop == 0 && odd.altitude_m == 0 && odd.seq == 0);
    CHECK(odd.heading_deg == 0 && !odd.heading_valid);   // a heading that is not a direction is not used
    GpsFix negative;
    parse_gps_json(R"({"version":1,"fix":true,"lat":1,"lon":2,"speed_kmph":-5,"heading_deg":-1,"heading_valid":true})", negative, problem);
    CHECK(negative.speed_kmph == 0 && negative.heading_deg == 0 && !negative.heading_valid);
    GpsFix edge;
    parse_gps_json(R"({"version":1,"fix":true,"lat":1,"lon":2,"heading_deg":359.9,"heading_valid":true})", edge, problem);
    CHECK_NEAR(edge.heading_deg, 359.9, 1e-9);
    CHECK(edge.heading_valid);
    GpsFix wrapped;
    parse_gps_json(R"({"version":1,"fix":true,"lat":1,"lon":2,"heading_deg":360,"heading_valid":true})", wrapped, problem);
    CHECK(!wrapped.heading_valid);
}

void feed_tests() {
    const fs::path dir = fs::temp_directory_path() / ("vtw_gps_feed_" + std::to_string((long)getpid()));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    const std::string file = (dir / "gps.json").string();
    const double T = 1.8e9;                              // a made-up "now" (seconds since 1970)

    // off: nothing named
    {
        GpsFeed off("");
        off.update(T);
        CHECK(off.state() == GpsState::Off);
        CHECK(gps_status_text(off, T).empty());
    }

    // no file
    {
        GpsFeed feed(file);
        CHECK(feed.state() == GpsState::NoFile);
        feed.update(T);
        CHECK(feed.state() == GpsState::NoFile);
        CHECK(!feed.has_data());
        CHECK(gps_status_text(feed, T).empty());         // nothing to say when there is no GPS at all
    }

    // a fresh file: a fix
    GpsFeed feed(file);
    put(file, fix_json(37.5, -97.5, true, 88, 87, true, 9, 7), T);
    feed.update(T + 0.1);
    CHECK(feed.state() == GpsState::Fix);
    CHECK(feed.has_data());
    CHECK_NEAR(feed.fix().lat, 37.5, 1e-9);
    CHECK_NEAR(feed.fix().lon, -97.5, 1e-9);
    CHECK_NEAR(feed.fix().speed_kmph, 88, 1e-9);
    CHECK(feed.fix().seq == 7);
    CHECK_NEAR(feed.age(T + 0.1), 0.1, 1e-6);
    CHECK(gps_status_text(feed, T + 0.1) == "gps: 88 km/h, heading 87, 9 satellites");

    // it is a fix until the file is more than 5 seconds old, then lost; nothing is asked of the file between polls
    feed.update(T + 4.9);
    CHECK(feed.state() == GpsState::Fix);
    feed.update(T + 5.0);                                // exactly 5 seconds old is still fresh
    CHECK(feed.state() == GpsState::Fix);
    feed.update(T + 5.1);
    CHECK(feed.state() == GpsState::Stale);
    CHECK(gps_status_text(feed, T + 5.1) == "gps: lost, nothing for 5 s");
    CHECK(feed.fix().lat == 37.5);                       // the last good reading is still there
    feed.update(T + 60);
    CHECK(gps_status_text(feed, T + 60) == "gps: lost, nothing for 60 s");

    // a new write brings it back, with the new values
    put(file, fix_json(38.0, -96.0, true, 20, 180, false, 11, 8), T + 61);
    feed.update(T + 61.2);
    CHECK(feed.state() == GpsState::Fix);
    CHECK_NEAR(feed.fix().lat, 38.0, 1e-9);
    CHECK(!feed.fix().heading_valid);
    CHECK(gps_status_text(feed, T + 61.2) == "gps: 20 km/h, 11 satellites");    // no heading while it is not valid

    // looked at about four times a second: a change is seen at the next look, not before
    put(file, fix_json(39.0, -96.0, true, 30, 10, true, 11, 9), T + 61.3);
    feed.update(T + 61.3);                               // 0.1 s since the last look
    CHECK_NEAR(feed.fix().lat, 38.0, 1e-9);
    feed.update(T + 61.4);                               // 0.2 s
    CHECK_NEAR(feed.fix().lat, 38.0, 1e-9);
    feed.update(T + 61.5);                               // 0.3 s: due
    CHECK_NEAR(feed.fix().lat, 39.0, 1e-9);

    // the freshness still moves on between looks
    put(file, fix_json(39.5, -96.0), T + 70);
    feed.update(T + 74.9);                               // looked at: 4.9 s old
    CHECK(feed.state() == GpsState::Fix);
    feed.update(T + 75.1);                               // only 0.2 s later, so not looked at, but 5.1 s old now
    CHECK(feed.state() == GpsState::Stale);

    // no fix: the GPS is there, without a position yet
    put(file, fix_json(39.5, -96.0, false, 0, 0, false, 3, 10), T + 80);
    feed.update(T + 80.1);
    CHECK(feed.state() == GpsState::NoFix);
    CHECK(gps_status_text(feed, T + 80.1) == "gps: no fix yet, 3 satellites");
    CHECK(feed.fix().fix == false);

    // a file of the real writer
    put(file, slurp(std::string(VTW_TEST_DATA_DIR) + "/gps_real_writer.json"), T + 90);
    feed.update(T + 90.1);
    CHECK(feed.state() == GpsState::Fix);
    CHECK_NEAR(feed.fix().lat, 37.6901235, 1e-9);
    CHECK_NEAR(feed.fix().speed_kmph, 88.6, 1e-9);
    CHECK(gps_status_text(feed, T + 90.1) == "gps: 89 km/h, heading 87, 9 satellites");

    // a damaged file: unreadable, saying why; the last good reading is kept; the next good file clears it
    put(file, "{\"version\": 1, \"fix\": tr", T + 100);
    feed.update(T + 100.1);
    CHECK(feed.state() == GpsState::Bad);
    CHECK(feed.problem() == "not valid JSON");
    CHECK_NEAR(feed.fix().lat, 37.6901235, 1e-9);
    CHECK(gps_status_text(feed, T + 100.1) == "gps: unreadable (not valid JSON)");
    put(file, R"({"version":2,"fix":true,"lat":1,"lon":2})", T + 101);
    feed.update(T + 101.1);
    CHECK(feed.state() == GpsState::Bad);
    CHECK(feed.problem().find("version 2") == 0);
    feed.update(T + 107);                                // damaged and old: lost, not "unreadable"
    CHECK(feed.state() == GpsState::Stale);
    put(file, fix_json(40.0, -95.0), T + 108);
    feed.update(T + 108.1);
    CHECK(feed.state() == GpsState::Fix);
    CHECK(feed.problem().empty());

    // a file that is far too big is not ours
    put(file, std::string(GPS_MAX_FILE_BYTES + 1, ' '), T + 110);
    feed.update(T + 110.1);
    CHECK(feed.state() == GpsState::Bad);
    CHECK(feed.problem() == "file is too big");
    put(file, fix_json(40.5, -95.0), T + 111);
    feed.update(T + 111.1);
    CHECK(feed.state() == GpsState::Fix);

    // a file that is not read: the same modification time means the same file, so it is not read again
    put(file, fix_json(41.0, -95.0), T + 120);
    feed.update(T + 120.1);
    CHECK_NEAR(feed.fix().lat, 41.0, 1e-9);
    put(file, fix_json(42.0, -95.0), T + 120);           // a different file with the very same time
    feed.update(T + 120.5);
    CHECK_NEAR(feed.fix().lat, 41.0, 1e-9);
    put(file, fix_json(42.0, -95.0), T + 120.25);        // a quarter of a second later: a new write
    feed.update(T + 120.8);
    CHECK_NEAR(feed.fix().lat, 42.0, 1e-9);

    // the file goes away (the writer's folder is emptied, say) and comes back
    CHECK(fs::remove(file));
    feed.update(T + 121.2);
    CHECK(feed.state() == GpsState::NoFile);
    CHECK(gps_status_text(feed, T + 121.2).empty());
    put(file, fix_json(43.0, -95.0), T + 120.25);        // the same time as before: it is read, because it was gone
    feed.update(T + 121.5);
    CHECK(feed.state() == GpsState::Fix);
    CHECK_NEAR(feed.fix().lat, 43.0, 1e-9);

    // a folder where the file should be is no file
    {
        const std::string folder = (dir / "folder.json").string();
        fs::create_directories(folder);
        GpsFeed f2(folder);
        f2.update(T);
        CHECK(f2.state() == GpsState::NoFile);
    }

    // a file left over from long ago is lost at once, never shown as a fix first
    {
        const std::string old_file = (dir / "old.json").string();
        put(old_file, fix_json(44.0, -95.0), T - 3600);
        GpsFeed f3(old_file);
        f3.update(T);
        CHECK(f3.state() == GpsState::Stale);
        CHECK(f3.has_data());
        CHECK(gps_status_text(f3, T).find("gps: lost, nothing for 3600 s") == 0);
    }

    // the clock moved: a file from the future (a clock set back) is lost, a little ahead is fine
    {
        const std::string clock_file = (dir / "clock.json").string();
        put(clock_file, fix_json(45.0, -95.0), T + 30);
        GpsFeed f4(clock_file);
        f4.update(T);
        CHECK(f4.state() == GpsState::Stale);
        f4.update(T + 26);                               // within the 5 s again
        CHECK(f4.state() == GpsState::Fix);
        f4.update(T + 34.5);
        CHECK(f4.state() == GpsState::Fix);
        f4.update(T + 35.5);
        CHECK(f4.state() == GpsState::Stale);
    }

    // time going backwards does not stop the looking
    {
        const std::string back_file = (dir / "back.json").string();
        put(back_file, fix_json(46.0, -95.0), T);
        GpsFeed f5(back_file);
        f5.update(T + 1000);
        put(back_file, fix_json(47.0, -95.0), T + 500);
        f5.update(T + 500.1);                            // earlier than the last look: it looks anyway
        CHECK_NEAR(f5.fix().lat, 47.0, 1e-9);
        CHECK(f5.state() == GpsState::Fix);
    }

    // a file that cannot be read (not as root, who can read anything)
    if (geteuid() != 0) {
        const std::string locked = (dir / "locked.json").string();
        put(locked, fix_json(48.0, -95.0), T);
        CHECK(chmod(locked.c_str(), 0000) == 0);
        GpsFeed f6(locked);
        f6.update(T + 0.1);
        CHECK(f6.state() == GpsState::Bad);
        CHECK(f6.problem() == "file could not be read");
        CHECK(!f6.has_data());
        CHECK(gps_status_text(f6, T + 0.1) == "gps: unreadable (file could not be read)");
    }

    fs::remove_all(dir, ignored);
}

}  // namespace

int main() {
    parser_tests();
    feed_tests();
    if (failures == 0) std::printf("all gps tests passed\n");
    return failures == 0 ? 0 : 1;
}
