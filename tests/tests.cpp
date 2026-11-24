// Tests for the View, the settings loader, the request policy, the radar scheduler and the keys. Plain asserts, no framework:
// the program prints what failed and exits with the number of failures.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"

#include "../src/input.h"
#include "../src/radar_scheduler.h"
#include "../src/request_policy.h"
#include "../src/settings.h"
#include "../src/view.h"

namespace {

using vtw::GeoPoint;
using vtw::View;

View make_view(double lat, double lon, double km, int cols = 100, int rows = 30) {
    View v;
    v.set_cell_aspect(2.0);
    v.set_centre({lat, lon});
    v.set_zoom_km(km);
    v.set_size(cols, rows);
    return v;
}

void view_tests() {
    // the middle of the screen is the centre
    {
        View v = make_view(37.69, -97.34, 80);
        const GeoPoint p = v.at(v.cols() / 2.0, v.rows() / 2.0);
        CHECK_NEAR(p.lat, 37.69, 1e-9);
        CHECK_NEAR(p.lon, -97.34, 1e-9);
    }
    // north is up, east is right
    {
        View v = make_view(37.69, -97.34, 80);
        CHECK(v.at(50, 0).lat > v.at(50, 30).lat);
        CHECK(v.at(100, 15).lon > v.at(0, 15).lon);
    }
    // km_across is the ground distance along the middle row of the screen, at any
    // latitude, zoom and size. That row follows a parallel, so it is measured along the
    // parallel (a great circle between its ends would be shorter on a wide view). The web
    // map's sphere has radius 6378.137 km.
    for (double lat : {0.0, 10.0, 37.69, 60.0, -45.0})
        for (double km : {5.0, 80.0, 500.0, 3000.0})
            for (int cols : {40, 100, 211}) {
                View v = make_view(lat, 12.0, km, cols, 30);
                const double lon_span = v.at(cols, 15).lon - v.at(0, 15).lon;
                const double along = 6378.137 * std::cos(lat * 3.14159265358979323846 / 180) *
                                     lon_span * 3.14159265358979323846 / 180;
                CHECK_NEAR(along / km, 1.0, 1e-6);
                // and for short distances it is the same as the great-circle distance
                if (km <= 80) CHECK_NEAR(vtw::distance_km(v.at(0, 15), v.at(cols, 15)) / km, 1.0, 0.002);
            }
    // a cell is twice as tall as it is wide, so a row covers twice the ground a column does
    {
        View v = make_view(37.69, -97.34, 80);
        const double across = vtw::distance_km(v.at(50, 15), v.at(51, 15));
        const double down = vtw::distance_km(v.at(50, 15), v.at(50, 16));
        CHECK_NEAR(down / across, 2.0, 0.01);
        CHECK_NEAR(v.km_per_col(), across, across * 0.002);
    }
    // from a place to a cell and back again, whatever the heading
    for (double heading : {0.0, 37.0, 90.0, 180.0, 271.0}) {
        View v = make_view(37.69, -97.34, 300);
        v.set_heading(heading);
        for (double col : {0.0, 13.5, 50.0, 99.0})
            for (double row : {0.0, 7.25, 15.0, 29.0}) {
                double c, r;
                v.to_cell(v.at(col, row), c, r);
                CHECK_NEAR(c, col, 1e-6);
                CHECK_NEAR(r, row, 1e-6);
            }
    }
    // the heading is the compass direction that is up the screen
    {
        View v = make_view(37.69, -97.34, 80);
        v.set_heading(90);                                    // facing east
        GeoPoint up = v.at(50, 0);
        CHECK(up.lon > -97.34);
        CHECK_NEAR(up.lat, 37.69, 0.01);
        v.set_heading(180);                                   // facing south
        up = v.at(50, 0);
        CHECK(up.lat < 37.69);
        CHECK_NEAR(up.lon, -97.34, 0.01);
    }
    // panning moves the view; zooming changes the distance, within the limits
    {
        View v = make_view(37.69, -97.34, 80);
        const GeoPoint before = v.centre();
        v.pan_cells(10, 0);
        double c, r;
        v.to_cell(before, c, r);
        CHECK_NEAR(c, 50 - 10, 1e-6);
        CHECK_NEAR(r, 15, 1e-6);
        CHECK(v.centre().lon > before.lon);

        v.zoom(0.5);
        CHECK_NEAR(v.zoom_km(), 40, 1e-9);
        v.set_zoom_km(0.5);
        CHECK_NEAR(v.zoom_km(), vtw::MIN_KM_ACROSS, 1e-9);
        v.set_zoom_km(1e9);
        CHECK_NEAR(v.zoom_km(), vtw::MAX_KM_ACROSS, 1e-9);
    }
    // the box holds the whole screen, and grows with the margin
    {
        View v = make_view(37.69, -97.34, 80);
        const vtw::GeoBox b = v.box();
        for (double col : {0.0, 100.0})
            for (double row : {0.0, 30.0}) {
                const GeoPoint p = v.at(col, row);
                CHECK(p.lat >= b.south - 1e-9 && p.lat <= b.north + 1e-9);
                CHECK(p.lon >= b.west - 1e-9 && p.lon <= b.east + 1e-9);
            }
        const vtw::GeoBox wide = v.box(0.2);
        CHECK(wide.west < b.west && wide.east > b.east && wide.south < b.south && wide.north > b.north);
        CHECK_NEAR((wide.east - wide.west) / (b.east - b.west), 1.4, 0.01);
    }
    // a centre is kept on the map: latitude limited, longitude wrapped
    {
        View v = make_view(0, 0, 80);
        v.set_centre({95, 190});
        CHECK_NEAR(v.centre().lat, vtw::MAX_LATITUDE, 1e-9);
        CHECK_NEAR(v.centre().lon, -170, 1e-9);
    }
}

std::string write_file(const char* name, const std::string& text) {
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream(path) << text;
    return path;
}

void settings_tests() {
    std::string notes;

    // a file that is not there: the defaults, and a note
    vtw::Settings s = vtw::load_settings("/no/such/settings.json", notes);
    CHECK(s.source == "built-in defaults");
    CHECK(notes.find("not found") != std::string::npos);

    // a good file
    notes.clear();
    s = vtw::load_settings(write_file("vtw_good.json",
        R"({"location":{"name":"Test","lat":10.5,"lon":-20.25},"zoom_km":300,"cell_aspect":1.8,"refresh_minutes":15,"user_agent_contact":"me"})"),
        notes);
    CHECK(notes.empty());
    CHECK(s.place_name == "Test");
    CHECK_NEAR(s.home.lat, 10.5, 1e-12);
    CHECK_NEAR(s.home.lon, -20.25, 1e-12);
    CHECK_NEAR(s.zoom_km, 300, 1e-12);
    CHECK_NEAR(s.cell_aspect, 1.8, 1e-12);
    CHECK(s.refresh_minutes == 15);
    CHECK(s.contact == "me");

    // not JSON at all
    notes.clear();
    s = vtw::load_settings(write_file("vtw_bad.json", "{ this is not json"), notes);
    CHECK(s.source == "built-in defaults");
    CHECK(notes.find("not valid JSON") != std::string::npos);

    // a value of the wrong type keeps its default; the rest is still read
    notes.clear();
    s = vtw::load_settings(write_file("vtw_type.json", R"({"zoom_km":"far","refresh_minutes":20})"), notes);
    CHECK(notes.find("zoom_km") != std::string::npos);
    CHECK_NEAR(s.zoom_km, 80, 1e-12);
    CHECK(s.refresh_minutes == 20);

    // values out of range are moved to the nearest allowed value, and never below the
    // refresh floor that protects the radar service
    notes.clear();
    s = vtw::load_settings(write_file("vtw_range.json",
        R"({"location":{"lat":120,"lon":500},"zoom_km":1,"cell_aspect":9,"refresh_minutes":1})"), notes);
    CHECK_NEAR(s.home.lat, vtw::MAX_LATITUDE, 1e-12);
    CHECK_NEAR(s.home.lon, 180, 1e-12);
    CHECK_NEAR(s.zoom_km, vtw::MIN_KM_ACROSS, 1e-12);
    CHECK_NEAR(s.cell_aspect, 3.0, 1e-12);
    CHECK(s.refresh_minutes == vtw::MIN_REFRESH_MINUTES);
    CHECK(notes.find("refresh_minutes") != std::string::npos);

    // --settings PATH is taken out of the command line
    {
        char a0[] = "prog", a1[] = "--no-color", a2[] = "--settings", a3[] = "my.json", a4[] = "--fps";
        char* argv[] = {a0, a1, a2, a3, a4, nullptr};
        int argc = 5;
        CHECK(vtw::take_settings_option(argc, argv) == "my.json");
        CHECK(argc == 3);
        CHECK(std::string(argv[1]) == "--no-color" && std::string(argv[2]) == "--fps");
        CHECK(vtw::take_settings_option(argc, argv).empty());
    }

    // with no path: settings.json, else settings.sample.json, else the defaults
    {
        namespace fs = std::filesystem;
        const fs::path old = fs::current_path();
        const fs::path dir = fs::temp_directory_path() / "vtw_cwd_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        fs::current_path(dir);
        notes.clear();
        CHECK(vtw::load_settings("", notes).source == "built-in defaults");
        std::ofstream("settings.sample.json") << R"({"zoom_km":111})";
        CHECK(vtw::load_settings("", notes).source == "settings.sample.json");
        std::ofstream("settings.json") << R"({"zoom_km":222})";
        s = vtw::load_settings("", notes);
        CHECK(s.source == "settings.json");
        CHECK_NEAR(s.zoom_km, 222, 1e-12);
        fs::current_path(old);
        fs::remove_all(dir);
    }
}


// The request policy, with a made-up clock (seconds)
void policy_tests() {
    using vtw::Decision;
    using vtw::RequestPolicy;

    // a new policy lets the first request go; a second one right after must wait for the gap
    {
        RequestPolicy p;
        CHECK(p.ask(100).kind == Decision::Go);
        p.started(100);
        p.succeeded(101);
        Decision d = p.ask(101.5);
        CHECK(d.kind == Decision::WaitThenGo);
        CHECK_NEAR(d.seconds, 1.5, 1e-9);                      // the gap is 2 s
        CHECK(p.ask(103.1).kind == Decision::Go);
    }
    // after a failure: 30 s, then 60, 120, ... up to 30 minutes, and success starts over
    {
        RequestPolicy p;
        double t = 1000;
        const double expected[] = {30, 60, 120, 240, 480, 960, 1800, 1800};
        for (double wait : expected) {
            p.started(t);
            p.failed(t + 1);
            t += 1;
            CHECK_NEAR(p.backoff_seconds(), wait, 1e-9);
            const Decision d = p.ask(t + 0.5);
            CHECK(d.kind == Decision::Refuse);
            CHECK_NEAR(d.seconds, wait - 0.5, 1e-9);
            CHECK(p.ask(t + wait + 0.001).kind == Decision::Go);
            t += wait + 3;
        }
        p.started(t);
        p.succeeded(t + 1);
        CHECK_NEAR(p.backoff_seconds(), 0, 1e-12);
        CHECK(p.ask(t + 4).kind == Decision::Go);
    }
    // a server that says Retry-After is obeyed when it asks for more than our backoff, but
    // never for longer than an hour
    {
        RequestPolicy p;
        p.started(0);
        p.failed(1, 300);
        CHECK_NEAR(p.ask(1).seconds, 300, 1e-9);
        p.started(400);
        p.failed(401, 99999);
        CHECK_NEAR(p.ask(401).seconds, 3600, 1e-9);
        p.started(5000);
        p.failed(5001, 5);                                       // less than the backoff: the backoff wins
        CHECK_NEAR(p.ask(5001).seconds, 120, 1e-9);
    }
    // giving up on a request is not the server's fault: no backoff
    {
        RequestPolicy p;
        p.started(10);
        p.cancelled(11);
        CHECK_NEAR(p.backoff_seconds(), 0, 1e-12);
        CHECK(p.ask(11.5).kind == Decision::WaitThenGo);
        CHECK(p.ask(13.1).kind == Decision::Go);
    }
    // the hourly limit, which frees up as old requests age out
    {
        vtw::PolicyLimits limits;
        limits.max_per_hour = 5;
        RequestPolicy p(limits);
        double t = 0;
        for (int i = 0; i < 5; i++) { CHECK(p.ask(t).kind == Decision::Go); p.started(t); p.succeeded(t + 0.1); t += 10; }
        const Decision d = p.ask(t);                              // t = 50
        CHECK(d.kind == Decision::Refuse);
        CHECK_NEAR(d.seconds, 3600 - 50, 1e-9);                   // until the first one is an hour old
        CHECK(p.ask(3600.5).kind == Decision::Go);
        CHECK(p.requests_in_last_hour(3600.5) == 4);
    }
}

// When to ask for a new radar picture, with a made-up clock (seconds)
void scheduler_tests() {
    using vtw::RadarScheduler;
    // the first picture is asked for at once, and not again until it is time
    {
        RadarScheduler s(300);
        CHECK(s.due(0, false, false));
        s.requested(0);
        s.finished(1, true, 0);
        CHECK(!s.due(2, true, true));                              // covered, not yet time
        CHECK(!s.due(299, true, true));
        CHECK(s.due(300, true, true));                             // the refresh time
        s.requested(300);
        CHECK(!s.due(301, true, true));
    }
    // moving somewhere the picture does not cover: only after staying there a moment
    {
        RadarScheduler s(300);
        s.requested(0);
        s.finished(1, true, 0);
        CHECK(!s.due(50, true, false));                            // just moved: starts the wait
        CHECK(!s.due(50.5, true, false));
        CHECK(s.due(51.1, true, false));                           // stayed a second: ask
        s.requested(51.1);
        CHECK(!s.due(52, true, false));                            // never twice within the spacing
        CHECK(!s.due(56, true, false));                            // still within the spacing
        CHECK(!s.due(57.2, true, false));                          // spacing over: the moment's wait starts again
        CHECK(s.due(58.3, true, false));
    }
    // moving about (never staying) asks for nothing; coming back resets the wait
    {
        RadarScheduler s(300);
        s.requested(0);
        s.finished(1, true, 0);
        for (double t = 10; t < 30; t += 0.4) {
            const bool covered = ((int)(t * 10) / 4) % 2 == 0;     // in and out every 0.4 s
            CHECK(!s.due(t, true, covered));
        }
        CHECK(!s.due(40, true, false));
        CHECK(!s.due(40.9, true, false));
        CHECK(!s.due(40.95, true, true));                          // back in view: the wait is forgotten
        CHECK(!s.due(41.5, true, false));
    }
    // after a failure: wait at least 10 s, or as long as the service asked, then try again
    {
        RadarScheduler s(300);
        CHECK(s.due(0, false, false));
        s.requested(0);
        s.finished(2, false, 0);
        CHECK(!s.due(5, false, false));
        CHECK(!s.due(11.9, false, false));
        CHECK(s.due(12.1, false, false));
        s.requested(12.1);
        s.finished(14, false, 120);                                // the service says: not for 2 minutes
        CHECK(!s.due(100, false, false));
        CHECK(s.due(134.5, false, false));
        // with a picture in hand a failed refresh is also tried again after the wait
        s.requested(134.5);
        s.finished(135, true, 0);
        s.requested(500);
        s.finished(501, false, 0);
        CHECK(!s.due(505, true, true));
        CHECK(s.due(511.5, true, true));
    }
}

// What the keys do
void input_tests() {
    using vtw::Command;
    CHECK(vtw::command_for_key('+') == Command::ZoomIn && vtw::command_for_key('=') == Command::ZoomIn);
    CHECK(vtw::command_for_key('-') == Command::ZoomOut && vtw::command_for_key('_') == Command::ZoomOut);
    CHECK(vtw::command_for_key('w') == Command::PanUp && vtw::command_for_key('W') == Command::PanUp);
    CHECK(vtw::command_for_key('s') == Command::PanDown && vtw::command_for_key('a') == Command::PanLeft);
    CHECK(vtw::command_for_key('d') == Command::PanRight && vtw::command_for_key('r') == Command::Reset);
    CHECK(vtw::command_for_key('x') == Command::None && vtw::command_for_key('\n') == Command::None);

    vtw::Settings settings;                                   // Wichita, 80 km
    View view = make_view(settings.home.lat, settings.home.lon, settings.zoom_km, 100, 30);

    // zoom: a step in shows 80 % of the distance, a step out undoes it
    vtw::apply_command(Command::ZoomIn, view, settings);
    CHECK_NEAR(view.zoom_km(), 64, 1e-9);
    vtw::apply_command(Command::ZoomOut, view, settings);
    CHECK_NEAR(view.zoom_km(), 80, 1e-9);
    for (int i = 0; i < 60; i++) vtw::apply_command(Command::ZoomIn, view, settings);
    CHECK_NEAR(view.zoom_km(), vtw::MIN_KM_ACROSS, 1e-9);     // stops at the closest zoom
    for (int i = 0; i < 80; i++) vtw::apply_command(Command::ZoomOut, view, settings);
    CHECK_NEAR(view.zoom_km(), vtw::MAX_KM_ACROSS, 1e-9);     // and the widest

    // pan: a tenth of the screen; d looks east, w looks north, and the screen follows
    view = make_view(settings.home.lat, settings.home.lon, settings.zoom_km, 100, 30);
    vtw::apply_command(Command::PanRight, view, settings);
    CHECK(view.centre().lon > settings.home.lon);
    CHECK_NEAR(view.centre().lat, settings.home.lat, 1e-9);
    double col, row;
    view.to_cell(settings.home, col, row);
    CHECK_NEAR(col, 50 - 10, 1e-6);                           // home is 10 columns left of the middle now
    vtw::apply_command(Command::PanLeft, view, settings);
    CHECK_NEAR(view.centre().lon, settings.home.lon, 1e-9);
    vtw::apply_command(Command::PanUp, view, settings);
    CHECK(view.centre().lat > settings.home.lat);
    view.to_cell(settings.home, col, row);
    CHECK_NEAR(row, 15 + 3, 0.01);                            // 3 rows (a tenth of 30) down from the middle (the scale shifts a hair with latitude)
    vtw::apply_command(Command::PanDown, view, settings);
    vtw::apply_command(Command::PanDown, view, settings);
    CHECK(view.centre().lat < settings.home.lat);

    // reset goes back to the settings' place and zoom
    vtw::apply_command(Command::ZoomIn, view, settings);
    vtw::apply_command(Command::PanRight, view, settings);
    vtw::apply_command(Command::Reset, view, settings);
    CHECK_NEAR(view.centre().lat, settings.home.lat, 1e-9);
    CHECK_NEAR(view.centre().lon, settings.home.lon, 1e-9);
    CHECK_NEAR(view.zoom_km(), settings.zoom_km, 1e-9);

    // the pan step is the same fraction of the screen at any zoom, so it covers more ground when zoomed out
    View near = make_view(37.69, -97.34, 10, 100, 30), far = make_view(37.69, -97.34, 1000, 100, 30);
    const GeoPoint n0 = near.centre(), f0 = far.centre();
    vtw::apply_command(Command::PanRight, near, settings);
    vtw::apply_command(Command::PanRight, far, settings);
    CHECK(vtw::distance_km(f0, far.centre()) > 50 * vtw::distance_km(n0, near.centre()));
    CHECK_NEAR(vtw::distance_km(n0, near.centre()), 1.0, 0.05);       // a tenth of 10 km

    // --offline and other flags are taken out of the command line
    {
        char a0[] = "prog", a1[] = "--offline", a2[] = "--fps", a3[] = "10";
        char* argv[] = {a0, a1, a2, a3, nullptr};
        int argc = 4;
        CHECK(vtw::take_flag(argc, argv, "--offline"));
        CHECK(argc == 3 && std::string(argv[1]) == "--fps" && std::string(argv[2]) == "10");
        CHECK(!vtw::take_flag(argc, argv, "--offline"));
        CHECK(argc == 3);
    }
}

}  // namespace

int main() {
    view_tests();
    settings_tests();
    policy_tests();
    scheduler_tests();
    input_tests();
    if (failures == 0) std::printf("all tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
