#include "orientation.h"

#include <algorithm>
#include <cmath>

namespace vtw {

namespace {

double wrap360(double degrees) {
    double a = std::fmod(degrees, 360.0);
    if (a < 0) a += 360.0;
    return a >= 360.0 ? 0.0 : a;
}

constexpr double LANDING_DEGREES = 0.05;        // closer than this to the target: it is there
constexpr double LONGEST_DT = 0.5;              // a frame that took longer than this (a stall) is treated as this long

}  // namespace

double heading_target(bool heading_up, bool following, const Car& car) {
    if (heading_up && following && car.arrow()) return wrap360(car.heading());
    return 0.0;
}

double shortest_turn(double from_deg, double to_deg) {
    double d = std::fmod(to_deg - from_deg, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

double HeadingSmoother::update(double target_deg, double dt) {
    const double target = wrap360(target_deg);
    const double left = shortest_turn(heading_, target);
    if (std::fabs(left) < LANDING_DEGREES) {
        heading_ = target;
        turning_ = false;
        return heading_;
    }
    if (!turning_) {
        if (target != 0.0 && std::fabs(left) < ENGAGE_DEGREES) return heading_;       // too small a change to bother the map with
        turning_ = true;
    }
    dt = std::min(std::max(dt, 0.0), LONGEST_DT);
    double step = left * (1.0 - std::exp(-TURN_SETTLE_PER_SECOND * dt));
    const double most = TURN_RATE_DEG_PER_SECOND * dt;
    step = std::min(std::max(step, -most), most);
    if (std::fabs(step) >= std::fabs(left)) step = left;       // never past it
    heading_ = wrap360(heading_ + step);
    if (std::fabs(shortest_turn(heading_, target)) < LANDING_DEGREES) {
        heading_ = target;
        turning_ = false;
    }
    return heading_;
}

}  // namespace vtw
