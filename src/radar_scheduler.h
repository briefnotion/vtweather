// RadarScheduler: decides when the program should ask for a new radar picture. It is pure
// (it is told the time and what it needs to know), so the rules can be tested with a made-up
// clock. The rules, kind to the service:
//   - the first picture as soon as the program starts;
//   - after that a new one only when the refresh time has passed (never under 5 minutes);
//   - or when the screen has moved to somewhere the picture does not cover, but only after it
//     has stayed there a moment, so panning and zooming do not make a request per key press;
//   - never two requests within a few seconds of each other;
//   - after a failure, wait before trying again (at least as long as the service asked).
#pragma once

namespace vtw {

constexpr double COVERAGE_DEBOUNCE_SECONDS = 1.0;
constexpr double MIN_SPACING_SECONDS = 5.0;       // between one request and the next
constexpr double MIN_RETRY_SECONDS = 10.0;        // after a failure

class RadarScheduler {
public:
    explicit RadarScheduler(double refresh_seconds) : refresh_seconds_(refresh_seconds) {}

    // Should a request go out now? have_frame: there is a picture already. covered: the picture
    // covers what is on screen. Call it once a frame while the worker is free.
    bool due(double now, bool have_frame, bool covered);

    void requested(double now);                       // a request was sent
    void finished(double now, bool ok, double retry_in);   // its result came back

    double refresh_at() const { return refresh_at_; }
    double not_before() const { return not_before_; }

private:
    double refresh_seconds_;
    double not_before_ = 0;          // no request before this
    double refresh_at_ = 0;          // time for a new picture
    double uncovered_since_ = -1;    // since when the screen has been outside the picture
};

}  // namespace vtw
