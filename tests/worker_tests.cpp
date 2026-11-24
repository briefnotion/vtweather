// Tests for the radar picture address and for the RadarWorker, against a server on this
// machine that serves the real NOAA pictures in tests/data. Nothing goes to the internet.
#include <chrono>
#include <string>
#include <thread>

#include "check.h"
#include "test_server.h"

#include "../src/radar_source.h"
#include "../src/radar_worker.h"

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
    s.limits.min_gap_seconds = 0.2;
    s.limits.first_backoff_seconds = 0.5;
    s.limits.max_backoff_seconds = 2;
    return s;
}

const vtw::GeoBox SEATTLE{47.0, -123.4, 48.2, -121.6};

// Poll take() the way the main loop does, once a frame, until a result arrives or time is up.
bool wait_for(vtw::RadarWorker& worker, vtw::RadarResult& result, double seconds = 5) {
    const auto start = Clock::now();
    while (since(start) < seconds) {
        if (worker.take(result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void source_tests() {
    // the address of a picture of the Seattle area
    const std::string url = vtw::radar_url(SEATTLE);
    CHECK(url.compare(0, 8, "https://") == 0);
    CHECK(url.find("opengeo.ncep.noaa.gov") != std::string::npos);
    CHECK(url.find("layers=conus:conus_bref_qcd") != std::string::npos);
    CHECK(url.find("bbox=-123.40000,47.00000,-121.60000,48.20000") != std::string::npos);
    CHECK(url.find("srs=EPSG:4326") != std::string::npos);
    CHECK(url.find("transparent=true") != std::string::npos);
    CHECK(url.find(' ') == std::string::npos);

    // about one pixel per kilometre: 1.8 degrees of longitude at 47.6 N is about 135 km
    int w, h;
    vtw::radar_picture_size(SEATTLE, w, h);
    CHECK(w >= 130 && w <= 140);
    CHECK(h >= 130 && h <= 137);
    // never smaller or larger than the limits, however close or far the view
    vtw::radar_picture_size(vtw::GeoBox{47.0, -122.0, 47.001, -121.999}, w, h);
    CHECK(w == vtw::MIN_PICTURE_SIDE && h == vtw::MIN_PICTURE_SIDE);
    vtw::radar_picture_size(vtw::GeoBox{20.0, -130.0, 55.0, -60.0}, w, h);
    CHECK(w == vtw::MAX_PICTURE_SIDE && h == vtw::MAX_PICTURE_SIDE);
    // a box off the edge of the earth is clipped
    const std::string wide = vtw::radar_url(vtw::GeoBox{-95.0, -200.0, 95.0, 200.0});
    CHECK(wide.find("bbox=-180.00000,-90.00000,180.00000,90.00000") != std::string::npos);
}

void worker_tests() {
    // a picture arrives: fetched, read and turned into a frame, and the worker is free again
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.can_request());
        CHECK(!worker.take(result));                          // nothing is waiting
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), SEATTLE}));
        CHECK(!worker.can_request());                         // busy until the result is taken
        CHECK(!worker.request({server.url("/ok"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok && result.asked && result.error.empty());
        CHECK(result.frame.width == 270 && result.frame.height == 180);
        int rain = 0;
        for (uint8_t v : result.frame.dbz) rain += v > 0;
        CHECK(rain == 47135);
        CHECK(result.box.north == SEATTLE.north && result.frame.box.west == SEATTLE.west);
        CHECK(worker.can_request());
        CHECK(!worker.take(result));                          // taken once
        // and a second request works, after the gap the policy asks for
        CHECK(worker.request({server.url("/png/noaa_kansas_clear"), vtw::GeoBox{36.0, -99.0, 39.0, -95.0}}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok && result.frame.width == 400);
        CHECK(server.requests() == 2);
    }
    // the service fails: the error comes back, and the next request is not even sent
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/status/503"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked);
        CHECK(result.error.find("503") != std::string::npos);
        CHECK(result.retry_in > 0);
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && !result.asked);
        CHECK(result.error.find("backing off") != std::string::npos);
        CHECK(server.requests() == 1);                       // the service was left alone
        std::this_thread::sleep_for(std::chrono::milliseconds(600));      // the backoff ends
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok);
    }
    // an answer that is not a picture, and a picture that is damaged
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::FetchSettings s = quick_settings();
        s.limits.first_backoff_seconds = 0.01;
        s.limits.min_gap_seconds = 0.01;
        vtw::RadarWorker worker(s);
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/xml"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked);
        CHECK(result.error.find("did not send a picture") != std::string::npos);
        CHECK(result.error.find("no such layer") != std::string::npos);
        CHECK(worker.request({server.url("/truncated/noaa_seattle_rain"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked);
        CHECK(result.error.find("could not be read") != std::string::npos);
        CHECK(result.frame.width == 0);
    }
    // quitting while a download is under way is quick
    {
        TestServer server(VTW_TEST_DATA_DIR);
        const auto start = Clock::now();
        {
            vtw::RadarWorker worker(quick_settings());
            CHECK(worker.request({server.url("/slow"), SEATTLE}));
            std::this_thread::sleep_for(std::chrono::milliseconds(300));        // it is in the download
        }
        CHECK(since(start) < 2.0);                           // not the 3 seconds the server takes
    }
    // quitting with nothing to do, and with a result nobody took
    {
        TestServer server(VTW_TEST_DATA_DIR);
        { vtw::RadarWorker idle(quick_settings()); }
        vtw::RadarWorker worker(quick_settings());
        CHECK(worker.request({server.url("/png/noaa_kansas_clear"), SEATTLE}));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    // many quick rounds of ask and take, as the main loop would do them
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::FetchSettings s = quick_settings();
        s.limits.min_gap_seconds = 0;
        s.limits.max_per_hour = 100000;
        vtw::RadarWorker worker(s);
        int received = 0;
        for (int round = 0; round < 40; round++) {
            vtw::RadarResult result;
            CHECK(worker.request({server.url("/png/noaa_kansas_clear"), vtw::GeoBox{36.0, -99.0, 39.0, -95.0}}));
            CHECK(wait_for(worker, result));
            received += result.ok;
        }
        CHECK(received == 40);
    }
}

}  // namespace

int main() {
    source_tests();
    worker_tests();
    if (failures == 0) std::printf("all radar worker tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
