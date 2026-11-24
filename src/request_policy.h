// RequestPolicy: when we may ask the network for something. The radar data comes from
// free services run by people who owe us nothing, so we are careful with them. This class
// holds the rules and knows nothing about the network: it is told the time (in seconds, on
// any steady clock) and says whether to go ahead, wait a moment, or not ask at all. That
// makes the rules easy to test with a made-up clock.
//
// The rules:
//   - a short gap between one request ending and the next starting;
//   - after a failure, wait before trying again: 30 s, then 1, 2, 4 minutes, up to 30
//     minutes, and longer if the server says so (Retry-After);
//   - never more than a set number of requests in an hour, as a safety net against a bug
//     that loops.
// One thread uses it at a time.
#pragma once

#include <deque>

namespace vtw {

struct PolicyLimits {
    double min_gap_seconds = 2.0;          // from one request ending to the next starting
    double first_backoff_seconds = 30;     // after a failure; doubles with each further one
    double max_backoff_seconds = 30 * 60;
    double max_retry_after_seconds = 60 * 60;    // the longest wait a server can ask of us
    int max_per_hour = 120;
};

struct Decision {
    enum Kind { Go, WaitThenGo, Refuse };
    Kind kind = Go;
    double seconds = 0;         // WaitThenGo: wait this long first. Refuse: not before this long.
    const char* reason = "";
};

class RequestPolicy {
public:
    explicit RequestPolicy(PolicyLimits limits = PolicyLimits()) : limits_(limits) {}

    Decision ask(double now) const;

    void started(double now);                      // a request is going out
    void succeeded(double now);                    // it came back fine
    void failed(double now, double retry_after = 0);   // it failed, or the server was struggling
    void cancelled(double now);                    // we gave up on it: not the server's fault

    double backoff_seconds() const { return backoff_; }   // 0 when all is well
    int requests_in_last_hour(double now) const;

private:
    PolicyLimits limits_;
    std::deque<double> starts_;        // when the requests of the last hour began
    double last_end_ = -1e18;
    double blocked_until_ = 0;
    double backoff_ = 0;
};

}  // namespace vtw
