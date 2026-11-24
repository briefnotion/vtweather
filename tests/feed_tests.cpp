// Tests for the RadarFeed: the worker and the scheduler working together, with a made-up
// clock for the schedule and a server on this machine serving the real NOAA test pictures.
// Nothing goes to the internet.
#include <chrono>
#include <string>
#include <thread>

#include "check.h"
#include "test_server.h"

#include "../src/radar_feed.h"

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

}  // namespace

int main() {
    tests();
    if (failures == 0) std::printf("all radar feed tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
