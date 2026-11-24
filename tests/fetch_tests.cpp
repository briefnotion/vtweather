// Tests for the Fetcher against a server on this machine (see test_server.h). They check
// that it asks politely: pacing, backing off after failures, the hourly limit, time and size
// limits, and giving up on request. Nothing goes to the internet.
#include <chrono>
#include <string>
#include <thread>

#include "check.h"
#include "test_server.h"

#include "../src/fetch.h"

namespace {

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Limits small enough to test quickly (the real ones are in PolicyLimits' defaults).
vtw::FetchSettings quick_settings() {
    vtw::FetchSettings s;
    s.user_agent = vtw::make_user_agent("test-contact");
    s.allow_plain_http = true;
    s.limits.min_gap_seconds = 0.2;
    s.limits.first_backoff_seconds = 0.4;
    s.limits.max_backoff_seconds = 1.6;
    return s;
}

void sleep_seconds(double s) { std::this_thread::sleep_for(std::chrono::duration<double>(s)); }

void tests() {
    // a good answer, and who we say we are
    {
        TestServer server;
        vtw::Fetcher fetcher(quick_settings());
        vtw::FetchResult r = fetcher.get(server.url("/ok"));
        CHECK(r.ok && r.asked && r.status == 200);
        CHECK(r.body.size() == 1000);
        CHECK(r.content_type == "image/png");
        r = fetcher.get(server.url("/agent"));
        CHECK(r.ok);
        CHECK(r.body == "vtweather/0.1 (+https://github.com/briefnotion/vtweather; test-contact)");
        CHECK(vtw::make_user_agent("") == "vtweather/0.1 (+https://github.com/briefnotion/vtweather)");
        // a redirect is followed
        r = fetcher.get(server.url("/redirect"));
        CHECK(r.ok && r.body.size() == 1000);
    }
    // only https unless a test says otherwise: nothing reaches the server
    {
        TestServer server;
        vtw::FetchSettings s = quick_settings();
        s.allow_plain_http = false;
        vtw::Fetcher fetcher(s);
        const vtw::FetchResult r = fetcher.get(server.url("/ok"));
        CHECK(!r.ok && !r.asked);
        CHECK(r.error.find("https") != std::string::npos);
        CHECK(server.requests() == 0);
        CHECK(!fetcher.get("ftp://example.com/x").asked);
    }
    // pacing: two requests in a row are at least the gap apart
    {
        TestServer server;
        vtw::Fetcher fetcher(quick_settings());
        const auto start = Clock::now();
        CHECK(fetcher.get(server.url("/ok")).ok);
        CHECK(fetcher.get(server.url("/ok")).ok);
        CHECK(since(start) >= 0.2);
        const auto third = Clock::now();
        CHECK(fetcher.get(server.url("/ok")).ok);
        CHECK(since(third) >= 0.15);
    }
    // after a failure the next request is refused without going out, until the backoff
    // is over; the backoff doubles with each failure and success clears it
    {
        TestServer server;
        vtw::Fetcher fetcher(quick_settings());
        vtw::FetchResult r = fetcher.get(server.url("/status/503"));
        CHECK(!r.ok && r.asked && r.status == 503);
        CHECK(r.retry_in > 0.2 && r.retry_in <= 0.4);
        const int seen = server.requests();
        r = fetcher.get(server.url("/ok"));
        CHECK(!r.ok && !r.asked);
        CHECK(r.error.find("backing off") != std::string::npos);
        CHECK(server.requests() == seen);                    // the server was left alone
        sleep_seconds(0.45);
        CHECK(fetcher.get(server.url("/status/500")).retry_in > 0.6);    // the second failure: 0.8 s
        sleep_seconds(0.9);
        CHECK(fetcher.get(server.url("/ok")).ok);
        CHECK(fetcher.get(server.url("/ok")).ok);            // all clear again
    }
    // a 404 and the other client errors count as failures too
    {
        TestServer server;
        vtw::Fetcher fetcher(quick_settings());
        const vtw::FetchResult r = fetcher.get(server.url("/nothing-here"));
        CHECK(!r.ok && r.status == 404);
        CHECK(!fetcher.get(server.url("/ok")).asked);
    }
    // Retry-After longer than our backoff is obeyed
    {
        TestServer server;
        vtw::Fetcher fetcher(quick_settings());
        const vtw::FetchResult r = fetcher.get(server.url("/status/429?retry=7"));
        CHECK(r.status == 429);
        CHECK(r.retry_in > 6.5 && r.retry_in <= 7.0);
    }
    // the hourly limit
    {
        TestServer server;
        vtw::FetchSettings s = quick_settings();
        s.limits.max_per_hour = 3;
        s.limits.min_gap_seconds = 0;
        vtw::Fetcher fetcher(s);
        for (int i = 0; i < 3; i++) CHECK(fetcher.get(server.url("/ok")).ok);
        const vtw::FetchResult r = fetcher.get(server.url("/ok"));
        CHECK(!r.ok && !r.asked);
        CHECK(r.error.find("hourly") != std::string::npos);
        CHECK(r.retry_in > 3500);
        CHECK(server.requests() == 3);
    }
    // a slow server: the request is given up after the time limit, and counts as a failure
    {
        TestServer server;
        vtw::FetchSettings s = quick_settings();
        s.total_timeout_seconds = 0.5;
        vtw::Fetcher fetcher(s);
        const auto start = Clock::now();
        const vtw::FetchResult r = fetcher.get(server.url("/slow"));
        CHECK(since(start) < 1.5);
        CHECK(!r.ok && r.asked);
        CHECK(r.error.find("timed out") != std::string::npos);
        CHECK(!fetcher.get(server.url("/ok")).asked);        // backing off
    }
    // too large an answer is cut off
    {
        TestServer server;
        vtw::FetchSettings s = quick_settings();
        s.max_body_bytes = 100000;
        vtw::Fetcher fetcher(s);
        const vtw::FetchResult r = fetcher.get(server.url("/big"));
        CHECK(!r.ok && r.body.empty());
        CHECK(r.error.find("too large") != std::string::npos);
    }
    // told to stop in the middle of a request: it returns quickly and is no failure
    {
        TestServer server;
        std::atomic<bool> stop{false};
        vtw::Fetcher fetcher(quick_settings(), [&stop] { return stop.load(); });
        std::thread stopper([&stop] { sleep_seconds(0.3); stop = true; });
        const auto start = Clock::now();
        const vtw::FetchResult r = fetcher.get(server.url("/slow"));
        stopper.join();
        CHECK(since(start) < 1.5);
        CHECK(!r.ok && r.error == "stopped");
        stop = false;
        CHECK(fetcher.get(server.url("/ok")).ok);            // no backoff was set
    }
    // an address nobody answers on: a clear error, and the policy backs off
    {
        vtw::FetchSettings s = quick_settings();
        s.connect_timeout_seconds = 1;
        vtw::Fetcher fetcher(s);
        const vtw::FetchResult r = fetcher.get("http://127.0.0.1:1/");      // nothing listens on port 1
        CHECK(!r.ok && r.asked && r.status == 0);
        CHECK(!r.error.empty());
        CHECK(!fetcher.get("http://127.0.0.1:1/").asked);
    }
}

}  // namespace

int main() {
    tests();
    if (failures == 0) std::printf("all fetch tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
