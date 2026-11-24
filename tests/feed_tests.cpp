// Tests for the RadarFeed: the worker and the scheduler working together, with a made-up
// clock for the schedule and a server on this machine serving the real NOAA test pictures.
// Nothing goes to the internet.
#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <thread>
#include <unistd.h>

#include "check.h"
#include "test_server.h"

#include "../src/radar_feed.h"
#include "../src/radar_source.h"

#ifndef VTW_TEST_DATA_DIR
#error "VTW_TEST_DATA_DIR must name the tests/data folder"
#endif

namespace {

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

vtw::FetchSettings quick_settings() {
    vtw::FetchSettings s;
    s.user_agent = vtw::make_user_agent("");
    s.allow_plain_http = true;
    s.limits.min_gap_seconds = 0.1;
    s.limits.first_backoff_seconds = 0.5;
    s.limits.max_backoff_seconds = 2;
    return s;
}

vtw::View make_view() {
    vtw::View view;
    view.set_cell_aspect(2.0);
    view.set_centre({47.6, -122.5});
    view.set_zoom_km(100);
    view.set_size(100, 30);
    return view;
}

bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Call update() the way the main loop does, a little real time and a little made-up time per
// frame, until the status says what we wait for.
bool wait_for_status(vtw::RadarFeed& feed, const vtw::View& view, double& t, const char* part,
                     double seconds = 5, double made_up_step = 0.01) {
    const auto start = Clock::now();
    while (since(start) < seconds) {
        feed.update(view, t);
        if (has(feed.status(t), part)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        t += made_up_step;
    }
    return false;
}

// Keep calling update() until the server has had n requests, and then a little longer so that
// the answer is taken in.
bool wait_for_requests(vtw::RadarFeed& feed, const vtw::View& view, double& t, const TestServer& server, int n) {
    const auto start = Clock::now();
    while (since(start) < 5) {
        feed.update(view, t);
        if (server.requests() >= n) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        t += 0.01;
    }
    for (int i = 0; i < 30; i++) {
        feed.update(view, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        t += 0.01;
    }
    return server.requests() == n;
}

void tests() {
    // the first picture arrives, then nothing more is asked until it is time
    {
        TestServer server(VTW_TEST_DATA_DIR);
        const vtw::View view = make_view();
        vtw::RadarFeed feed(quick_settings(), 300, [&](const vtw::GeoBox&) { return server.url("/png/noaa_seattle_rain"); });
        double t = 0;
        feed.update(view, t);
        CHECK(has(feed.status(t), "asking"));
        CHECK(!feed.has_frame());
        CHECK(wait_for_status(feed, view, t, "just fetched"));
        CHECK(feed.has_frame() && !feed.failing());
        CHECK(feed.frame().width == 270 && feed.frame().height == 180);
        CHECK(server.requests() == 1);

        for (t = 1; t < 299; t += 1) feed.update(view, t);          // covered, not yet time
        CHECK(server.requests() == 1);
        CHECK(has(feed.status(299), "min ago"));

        // the refresh time: a second request
        t = 300.5;
        CHECK(wait_for_requests(feed, view, t, server, 2));
        CHECK(has(feed.status(t), "just fetched"));

        // moving off the picture: nothing for the first moment, then a request
        vtw::View moved = view;
        moved.pan_cells(400, 0);
        t = 320;
        feed.update(moved, t);
        feed.update(moved, t + 0.5);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(server.requests() == 2);
        t = 321.2;
        CHECK(wait_for_requests(feed, moved, t, server, 3));
        CHECK(!feed.failing());

        // back to where you were a moment ago: the cached picture is used, nothing is downloaded
        const double back_at = t + 20;
        for (t = back_at; t < back_at + 10; t += 0.5) {
            feed.update(view, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(server.requests() == 3);
        CHECK(feed.has_frame() && !feed.failing());
        CHECK(feed.cached_pictures() >= 2);
        // and away and back again, still within the refresh time: still nothing
        for (t = back_at + 10; t < back_at + 15; t += 0.5) feed.update(moved, t);
        for (t = back_at + 15; t < back_at + 25; t += 0.5) feed.update(view, t);
        CHECK(server.requests() == 3);

        // once the cached picture is stale, it is still shown while a new one is asked for
        t = 301 + 400;
        feed.update(view, t);
        CHECK(feed.has_frame());
        CHECK(wait_for_requests(feed, view, t, server, 4));
    }
    // a service that fails: the feed says so, keeps asking only now and then, and is told
    // to leave the service alone while it backs off
    {
        TestServer server(VTW_TEST_DATA_DIR);
        const vtw::View view = make_view();
        bool good = false;
        vtw::RadarFeed feed(quick_settings(), 300, [&](const vtw::GeoBox&) {
            return server.url(good ? "/png/noaa_seattle_rain" : "/status/503");
        });
        double t = 0;
        CHECK(wait_for_status(feed, view, t, "no picture yet"));
        CHECK(has(feed.status(t), "503"));
        CHECK(has(feed.status(t), "next try in"));
        CHECK(feed.failing() && !feed.has_frame());
        const double failed_at = t;
        // a minute of made-up time, a little real time with each step
        while (t < failed_at + 60) {
            feed.update(view, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            t += 0.5;
        }
        CHECK(server.requests() >= 1 && server.requests() <= 3);
        CHECK(!feed.has_frame());
        // the service recovers: the next try works and the trouble is forgotten
        good = true;
        t += 30;
        CHECK(wait_for_status(feed, view, t, "just fetched", 8, 0.5));       // made-up time runs faster here
        CHECK(feed.has_frame() && !feed.failing());
    }
}

// the radar covers only part of the earth: nothing is asked for outside it, and what is asked for is
// exactly the part of the view inside it
void coverage_tests() {
    namespace fs = std::filesystem;
    TestServer server(VTW_TEST_DATA_DIR);
    std::vector<vtw::GeoBox> asked;                                          // every area the feed asked a picture of
    auto url = [&](const vtw::GeoBox& area) { asked.push_back(area); return server.url("/png/noaa_seattle_rain"); };
    auto make = [](double lat, double lon, double km, int cols = 120, int rows = 40) {
        vtw::View v;
        v.set_cell_aspect(2.0);
        v.set_centre({lat, lon});
        v.set_zoom_km(km);
        v.set_size(cols, rows);
        return v;
    };
    auto inside_radar = [](const vtw::GeoBox& b) {
        return b.south >= vtw::RADAR_COVERAGE.south && b.north <= vtw::RADAR_COVERAGE.north &&
               b.west >= vtw::RADAR_COVERAGE.west && b.east <= vtw::RADAR_COVERAGE.east;
    };

    // near the north pole (the view that once broke it): nothing is asked for, and it says why. (At the
    // widest zoom the bottom of such a screen still reaches down into the radar's area: Mercator
    // stretches the north so much; that case is below.)
    {
        vtw::RadarFeed feed(quick_settings(), 300, url);
        for (double km : {1500.0, 600.0, 50.0}) {
            const vtw::View pole = make(85, -107.9, km);                       // the view clamps the centre to 80
            double t = 0;
            for (int i = 0; i < 100; i++) { feed.update(pole, t); t += 1; }
            CHECK(feed.outside_coverage() && !feed.has_frame());
            CHECK(has(feed.status(t), "none here"));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(server.requests() == 0 && asked.empty());
    }
    // a view that only just reaches the radar's area: the area asked for is inside it, and the picture is
    // labelled with that same area (so it can never claim more than it shows); a refresh is not asked for
    // again and again because the view extends past the edge
    {
        asked.clear();
        vtw::RadarFeed feed(quick_settings(), 300, url);
        const vtw::View edge = make(57, -62, 800);                              // mostly north-east of the radar
        double t = 0;
        CHECK(wait_for_status(feed, edge, t, "just fetched"));
        CHECK(asked.size() == 1 && inside_radar(asked[0]));
        CHECK(feed.has_frame() && inside_radar(feed.frame().box));
        CHECK(feed.frame().box.north == asked[0].north && feed.frame().box.west == asked[0].west);
        const int requests = server.requests();
        for (t = t + 1; t < 200; t += 1) feed.update(edge, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(server.requests() == requests);
    }
    // the whole country, and further out than the radar sees: still one picture, inside the radar's area
    {
        asked.clear();
        vtw::RadarFeed feed(quick_settings(), 300, url);
        const vtw::View wide = make(40, -95, 6000);
        double t = 0;
        CHECK(wait_for_status(feed, wide, t, "just fetched"));
        CHECK(asked.size() == 1 && inside_radar(asked[0]));
        const int requests = server.requests();
        for (t = t + 1; t < 200; t += 1) feed.update(wide, t);
        CHECK(server.requests() == requests);
        // out over the open Pacific: nothing asked, and the picture is still there for when you come back
        const vtw::View sea = make(10, -170, 400);
        feed.update(sea, t);
        CHECK(feed.outside_coverage() && has(feed.status(t), "none here"));
        feed.update(wide, t + 1);
        CHECK(!feed.outside_coverage() && feed.has_frame());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(server.requests() == requests);
    }
    // the view that once broke it, at the widest zoom near the pole: the bottom of the screen reaches the radar's
    // area, and what is asked for is only that part
    {
        asked.clear();
        vtw::RadarFeed feed(quick_settings(), 300, url);
        const vtw::View pole = make(85, -107.9, 6000);
        double t = 0;
        CHECK(wait_for_status(feed, pole, t, "just fetched"));
        CHECK(asked.size() == 1 && inside_radar(asked[0]));
        CHECK(inside_radar(feed.frame().box));
        const int requests = server.requests();
        for (t = t + 1; t < 200; t += 1) feed.update(pole, t);
        CHECK(server.requests() == requests);
    }
    // random views anywhere on the earth: every area asked for is inside the radar's
    {
        asked.clear();
        std::mt19937 rng(7);
        for (int round = 0; round < 40; round++) {
            vtw::RadarFeed feed(quick_settings(), 300, url);
            const vtw::View v = make(-85 + (rng() % 1700) / 10.0, -180 + (rng() % 3600) / 10.0, std::exp(std::log(2.0) + std::log(3000.0) * (rng() % 1000) / 1000.0));
            double t = 0;
            for (int i = 0; i < 3; i++) { feed.update(v, t); t += 0.05; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        }
        for (const vtw::GeoBox& a : asked) CHECK(inside_radar(a));
    }
    // a picture saved by an older version that claims an area outside the radar's is not used
    {
        const std::string folder = (fs::temp_directory_path() / ("vtw_feed_bad_" + std::to_string(getpid()))).string();
        fs::remove_all(folder);
        vtw::RadarStore store(folder);
        vtw::RadarFrame bad;
        bad.box = {-67.84, -571.7, 89.96, 355.9};                              // what the pole once produced
        bad.width = 2; bad.height = 2;
        bad.dbz = {1, 2, 3, 4};
        std::string error;
        CHECK(store.save(bad, 1800000000, 0, error));
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, 1800000030);
        feed.update(make(40, -95, 6000), 0);
        CHECK(!feed.has_frame());                                              // not taken for a picture of anything
        fs::remove_all(folder);
    }
}

// the real scan time in the status line, and kept with the picture
void scan_status_tests() {
    namespace fs = std::filesystem;
    setenv("TZ", "UTC", 1);                                   // so that local time is UTC in this test
    tzset();
    const std::string folder = (fs::temp_directory_path() / ("vtw_feed_scan_" + std::to_string(getpid()))).string();
    fs::remove_all(folder);
    const vtw::View view = make_view();
    TestServer server(VTW_TEST_DATA_DIR);
    auto url = [&](const vtw::GeoBox&) { return server.url("/png/noaa_seattle_rain"); };
    const double scan = 1795518823.0;                          // 2026-11-24 11:13:43 UTC, in the capabilities
    const double start = scan + 130;                           // two minutes and ten seconds later

    {
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start, server.url("/caps"));
        double t = 0;
        CHECK(wait_for_status(feed, view, t, "scan 11:13 (11:13Z), 2 min old"));
        CHECK(server.paths().size() == 2 && server.paths()[0] == "/caps");
    }
    {   // the scan time was kept with the picture: a later run shows how old the scan is, offline too
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start + 200, server.url("/caps"));
        feed.set_offline(true);
        feed.update(view, 0);
        CHECK(feed.has_frame());
        CHECK(has(feed.status(0), "scan 11:13 (11:13Z), 5 min old"));
        CHECK(has(feed.status(0), "--offline"));
    }
    fs::remove_all(folder);
}

// pictures kept between runs
void store_tests() {
    namespace fs = std::filesystem;
    const std::string folder = (fs::temp_directory_path() / ("vtw_feed_store_" + std::to_string(getpid()))).string();
    fs::remove_all(folder);
    const vtw::View view = make_view();
    TestServer server(VTW_TEST_DATA_DIR);
    auto url = [&](const vtw::GeoBox&) { return server.url("/png/noaa_seattle_rain"); };
    const double start = 1800000000;                                       // a made-up clock time for the first run

    {   // the first run downloads a picture, and keeps it on disk
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start);
        double t = 0;
        CHECK(wait_for_status(feed, view, t, "just fetched"));
        CHECK(server.requests() == 1);
    }
    CHECK(fs::exists(folder + "/radar_0.bin") || fs::exists(folder + "/radar_1.bin") || fs::exists(folder + "/radar_2.bin") || fs::exists(folder + "/radar_3.bin"));

    {   // a run a little over two minutes later shows it at once and, while it is fresh, asks for nothing
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start + 130);
        double t = 0;
        feed.update(view, t);
        CHECK(feed.has_frame());
        CHECK(has(feed.status(t), "2 min ago"));
        CHECK(feed.frame().width == 270);
        for (t = 1; t < 160; t += 1) feed.update(view, t);                 // up to 4.8 minutes old
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(server.requests() == 1);
    }
    {   // a picture that has gone stale is shown at once and a new one is asked for
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start + 400);
        double t = 0;
        feed.update(view, t);
        CHECK(feed.has_frame() && has(feed.status(t), "(old)"));
        CHECK(wait_for_requests(feed, view, t, server, 2));
        CHECK(has(feed.status(t), "just fetched"));
    }
    {   // offline: the saved picture is shown, however old, and nothing is ever asked for
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start + 5000);
        feed.set_offline(true);
        double t = 0;
        for (int i = 0; i < 50; i++) { feed.update(view, t); t += 1; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(feed.has_frame());
        CHECK(has(feed.status(t), "--offline") && has(feed.status(t), "saved picture"));
        CHECK(server.requests() == 2);
    }
    {   // offline with nothing saved: it says so
        vtw::RadarStore empty((fs::path(folder) / "nothing_here").string());
        vtw::RadarFeed feed(quick_settings(), 300, url, &empty, start);
        feed.set_offline(true);
        feed.update(view, 0);
        CHECK(!feed.has_frame() && has(feed.status(0), "no saved picture"));
    }
    {   // a picture older than the limit is not loaded: it is asked for again
        vtw::RadarStore store(folder);
        vtw::RadarFeed feed(quick_settings(), 300, url, &store, start + vtw::STORED_MAX_AGE_SECONDS + 100000);
        double t = 0;
        feed.update(view, t);
        CHECK(!feed.has_frame());
        CHECK(wait_for_requests(feed, view, t, server, 3));
    }
    fs::remove_all(folder);
}

}  // namespace

int main() {
    tests();
    coverage_tests();
    scan_status_tests();
    store_tests();
    if (failures == 0) std::printf("all radar feed tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
