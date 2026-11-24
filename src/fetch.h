// Fetcher: the one place in the program that talks to the web. Every request goes through
// get(), and get() follows the RequestPolicy (pacing, backing off after failures, an hourly
// limit), so no other code can be impolite by accident.
//
// It uses libcurl. Only https addresses are accepted. A request has time limits and a size
// limit, and can be cancelled from outside (the stop check is looked at while data is
// moving). One thread uses a Fetcher at a time, and get() blocks: it is meant to run on the
// worker thread, never on the thread that draws.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "request_policy.h"

namespace vtw {

struct FetchResult {
    bool ok = false;           // a 2xx answer arrived whole
    bool asked = false;        // a request really went out (false: the policy or a check said no)
    int status = 0;            // the HTTP status, 0 if there was no answer
    std::string body;
    std::string content_type;
    std::string error;         // why it failed, in words; empty when ok
    double retry_in = 0;       // after a failure or a refusal: seconds until we will try again
};

struct FetchSettings {
    std::string user_agent;                    // see make_user_agent()
    PolicyLimits limits;
    double connect_timeout_seconds = 10;
    double total_timeout_seconds = 30;
    size_t max_body_bytes = 8 * 1024 * 1024;
    bool allow_plain_http = false;             // only for tests against a server on this machine
};

class Fetcher {
public:
    // should_stop is asked now and then during a request and while waiting; if it says
    // true the request is given up (and that is not counted as a failure of the server).
    using StopCheck = std::function<bool()>;

    explicit Fetcher(FetchSettings settings, StopCheck should_stop = StopCheck());
    ~Fetcher();
    Fetcher(const Fetcher&) = delete;
    Fetcher& operator=(const Fetcher&) = delete;

    FetchResult get(const std::string& url);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// "vtweather/0.1 (+https://github.com/briefnotion/vtweather; contact)": who is asking and
// where to find the program; the contact, if there is one, comes from the settings.
std::string make_user_agent(const std::string& contact);

}  // namespace vtw
