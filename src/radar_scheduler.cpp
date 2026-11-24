#include "radar_scheduler.h"

#include <algorithm>

namespace vtw {

bool RadarScheduler::due(double now, bool have_frame, bool covered) {
    if (now < not_before_) return false;
    if (!have_frame) return true;
    if (now >= refresh_at_) return true;
    if (covered) {
        uncovered_since_ = -1;
        return false;
    }
    if (uncovered_since_ < 0) uncovered_since_ = now;
    return now - uncovered_since_ >= COVERAGE_DEBOUNCE_SECONDS;
}

void RadarScheduler::requested(double now) {
    not_before_ = now + MIN_SPACING_SECONDS;
    refresh_at_ = now + refresh_seconds_;
    uncovered_since_ = -1;
}

void RadarScheduler::finished(double now, bool ok, double retry_in) {
    if (ok) return;
    not_before_ = std::max(not_before_, now + std::max(retry_in, MIN_RETRY_SECONDS));
    refresh_at_ = not_before_;                         // and then try again
}

}  // namespace vtw
