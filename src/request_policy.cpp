#include "request_policy.h"

#include <algorithm>

namespace vtw {

namespace {
constexpr double HOUR = 3600.0;
}

int RequestPolicy::requests_in_last_hour(double now) const {
    return (int)std::count_if(starts_.begin(), starts_.end(),
                              [now](double t) { return t > now - HOUR; });
}

Decision RequestPolicy::ask(double now) const {
    if (now < blocked_until_)
        return {Decision::Refuse, blocked_until_ - now, "backing off after a failure"};

    if (requests_in_last_hour(now) >= limits_.max_per_hour) {
        double oldest = now;
        for (double t : starts_)
            if (t > now - HOUR) { oldest = t; break; }
        return {Decision::Refuse, oldest + HOUR - now, "the hourly request limit is reached"};
    }

    const double since_last = now - last_end_;
    if (since_last < limits_.min_gap_seconds)
        return {Decision::WaitThenGo, limits_.min_gap_seconds - since_last, "pacing"};

    return {Decision::Go, 0, ""};
}

void RequestPolicy::started(double now) {
    while (!starts_.empty() && starts_.front() <= now - HOUR) starts_.pop_front();
    starts_.push_back(now);
}

void RequestPolicy::succeeded(double now) {
    last_end_ = now;
    backoff_ = 0;
    blocked_until_ = 0;
}

void RequestPolicy::failed(double now, double retry_after) {
    last_end_ = now;
    backoff_ = backoff_ == 0 ? limits_.first_backoff_seconds
                             : std::min(backoff_ * 2, limits_.max_backoff_seconds);
    const double asked = std::min(std::max(retry_after, 0.0), limits_.max_retry_after_seconds);
    blocked_until_ = now + std::max(backoff_, asked);
}

void RequestPolicy::cancelled(double now) { last_end_ = now; }

}  // namespace vtw
