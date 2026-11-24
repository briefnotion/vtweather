// Tests for the radar picture address and for the RadarWorker, against a server on this
// machine that serves the real NOAA pictures in tests/data. Nothing goes to the internet.
#include <chrono>
#include <fstream>
#include <sstream>
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

void box_tests() {
    // cut to the earth
    const vtw::GeoBox over = vtw::clip_to_earth(vtw::GeoBox{-95, -571, 120, 355});
    CHECK(over.south == -90 && over.north == 90 && over.west == -180 && over.east == 180);
    const vtw::GeoBox inside = vtw::clip_to_earth(vtw::GeoBox{10, -20, 30, 40});
    CHECK(inside.south == 10 && inside.west == -20 && inside.north == 30 && inside.east == 40);
    // what two boxes share
    vtw::GeoBox out;
    CHECK(vtw::intersect({0, 0, 10, 10}, {5, 5, 15, 15}, out) && out.south == 5 && out.west == 5 && out.north == 10 && out.east == 10);
    CHECK(vtw::intersect({0, 0, 10, 10}, {2, 2, 3, 3}, out) && out.north == 3);                     // one inside the other
    CHECK(!vtw::intersect({0, 0, 10, 10}, {20, 20, 30, 30}, out));                                  // apart
    CHECK(!vtw::intersect({0, 0, 10, 10}, {10, 0, 20, 10}, out));                                   // touching only
    // the radar's area, and a view of the whole US inside it
    CHECK(vtw::RADAR_COVERAGE.south == 20 && vtw::RADAR_COVERAGE.north == 55);
    CHECK(vtw::intersect({-90, -180, 90, 180}, vtw::RADAR_COVERAGE, out) && out.west == -130 && out.east == -60);
    // the address asks for exactly the clipped box: what the picture shows is what it is labelled with
    const vtw::GeoBox huge{-67.84, -571.7, 89.96, 355.9};
    const std::string url = vtw::radar_url(huge);
    CHECK(url.find("bbox=-180.00000,-67.84000,180.00000,89.96000") != std::string::npos);
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

void scan_time_tests() {
    // the real capabilities document from NOAA (tests/data): the radar layer's time is the one we want,
    // not that of the other layers in the same document
    std::ifstream in(std::string(VTW_TEST_DATA_DIR) + "/noaa_capabilities_conus.xml", std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    const std::string caps = bytes.str();
    CHECK(caps.size() > 10000);
    vtw::ScanTime scan;
    CHECK(vtw::parse_scan_time(caps, vtw::RADAR_LAYER, scan));
    CHECK(scan.iso == "2026-11-24T11:13:43Z");
    CHECK_NEAR(scan.unix_time, 1795518823.0, 1e-6);
    vtw::ScanTime other;
    CHECK(vtw::parse_scan_time(caps, "conus_cref_qcd", other));
    CHECK(other.iso == "2026-11-24T11:14:43Z" && other.unix_time == scan.unix_time + 60);
    CHECK(vtw::parse_scan_time(caps, "conus_pcpn_typ", other) && other.iso == "2026-11-24T11:14:00Z");
    // a layer that is not there, a layer with no time, and text that is no capabilities document
    CHECK(!vtw::parse_scan_time(caps, "no_such_layer", other) && other.iso.empty());
    CHECK(!vtw::parse_scan_time(caps, "radar_precip_type", other));          // named in the document, but its block has no time
    CHECK(!vtw::parse_scan_time("", vtw::RADAR_LAYER, other));
    CHECK(!vtw::parse_scan_time("<html>nothing</html>", vtw::RADAR_LAYER, other));
    // the time formats the service may write, and damaged ones
    auto doc = [](const std::string& time) {
        return "<Layer><Name>conus_bref_qcd</Name><Dimension name=\"time\" default=\"" + time + "\" units=\"ISO8601\">a,b</Dimension></Layer>";
    };
    CHECK(vtw::parse_scan_time(doc("2026-01-02T03:04:05Z"), vtw::RADAR_LAYER, other));
    CHECK_NEAR(other.unix_time, 1767323045.0, 1e-6);
    CHECK(vtw::parse_scan_time(doc("2026-01-02T03:04:05.250Z"), vtw::RADAR_LAYER, other));
    CHECK_NEAR(other.unix_time, 1767323045.25, 1e-6);
    for (const char* bad : {"", "yesterday", "2026-13-02T03:04:05Z", "2026-01-32T03:04:05Z", "2026-01-02T25:04:05Z", "1969-01-02T03:04:05Z",
                            "2026-01-02", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"})
        CHECK(!vtw::parse_scan_time(doc(bad), vtw::RADAR_LAYER, other));
    // the time goes onto the picture address
    CHECK(vtw::with_scan_time("https://x/y?a=1", "2026-01-02T03:04:05Z") == "https://x/y?a=1&time=2026-01-02T03:04:05Z");
    CHECK(vtw::with_scan_time("https://x/y", "T") == "https://x/y?time=T");
    CHECK(vtw::radar_time_url().compare(0, 8, "https://") == 0 && vtw::radar_time_url().find("GetCapabilities") != std::string::npos);
}

void worker_tests() {
    // a picture arrives: fetched, read and turned into a frame, and the worker is free again
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.can_request());
        CHECK(!worker.take(result));                          // nothing is waiting
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), "", SEATTLE}));
        CHECK(!worker.can_request());                         // busy until the result is taken
        CHECK(!worker.request({server.url("/ok"), "", SEATTLE}));
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
        CHECK(worker.request({server.url("/png/noaa_kansas_clear"), "", vtw::GeoBox{36.0, -99.0, 39.0, -95.0}}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok && result.frame.width == 400);
        CHECK(server.requests() == 2);
    }
    // with a time address: it is asked first, and the picture is asked for at that scan time
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), server.url("/caps"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok && result.asked);
        CHECK_NEAR(result.scan_unix, 1795518823.0, 1e-6);
        const std::vector<std::string> paths = server.paths();
        CHECK(paths.size() == 2);
        if (paths.size() == 2) {
            CHECK(paths[0] == "/caps");
            CHECK(paths[1] == "/png/noaa_seattle_rain?time=2026-11-24T11:13:43Z");
        }
    }
    {   // a capabilities document that cannot be read: the newest picture is asked for anyway, scan time unknown
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), server.url("/garbage"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(result.ok && result.scan_unix == 0);
        CHECK(server.paths().size() == 2 && server.paths()[1] == "/png/noaa_seattle_rain");
    }
    {   // the capabilities fail: the error comes back and no picture is asked for
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), server.url("/status/503"), SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked && result.error.find("503") != std::string::npos);
        CHECK(server.requests() == 1);
    }
    // the service fails: the error comes back, and the next request is not even sent
    {
        TestServer server(VTW_TEST_DATA_DIR);
        vtw::RadarWorker worker(quick_settings());
        vtw::RadarResult result;
        CHECK(worker.request({server.url("/status/503"), "", SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked);
        CHECK(result.error.find("503") != std::string::npos);
        CHECK(result.retry_in > 0);
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), "", SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && !result.asked);
        CHECK(result.error.find("backing off") != std::string::npos);
        CHECK(server.requests() == 1);                       // the service was left alone
        std::this_thread::sleep_for(std::chrono::milliseconds(600));      // the backoff ends
        CHECK(worker.request({server.url("/png/noaa_seattle_rain"), "", SEATTLE}));
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
        CHECK(worker.request({server.url("/xml"), "", SEATTLE}));
        CHECK(wait_for(worker, result));
        CHECK(!result.ok && result.asked);
        CHECK(result.error.find("did not send a picture") != std::string::npos);
        CHECK(result.error.find("no such layer") != std::string::npos);
        CHECK(worker.request({server.url("/truncated/noaa_seattle_rain"), "", SEATTLE}));
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
            CHECK(worker.request({server.url("/slow"), "", SEATTLE}));
            std::this_thread::sleep_for(std::chrono::milliseconds(300));        // it is in the download
        }
        CHECK(since(start) < 2.0);                           // not the 3 seconds the server takes
    }
    // quitting with nothing to do, and with a result nobody took
    {
        TestServer server(VTW_TEST_DATA_DIR);
        { vtw::RadarWorker idle(quick_settings()); }
        vtw::RadarWorker worker(quick_settings());
        CHECK(worker.request({server.url("/png/noaa_kansas_clear"), "", SEATTLE}));
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
            CHECK(worker.request({server.url("/png/noaa_kansas_clear"), "", vtw::GeoBox{36.0, -99.0, 39.0, -95.0}}));
            CHECK(wait_for(worker, result));
            received += result.ok;
        }
        CHECK(received == 40);
    }
}

}  // namespace

int main() {
    box_tests();
    source_tests();
    scan_time_tests();
    worker_tests();
    if (failures == 0) std::printf("all radar worker tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
