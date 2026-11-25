// Orientation: which way is up on the map. Normally north is (north up). With heading up (h) the map is turned so
// that the way the car is travelling is up the screen, like a car's navigation screen, but only while it makes sense:
//
//   - the map is following the car (f), since turning the map about some other place is no use, and
//   - the car counts as moving and has a direction (Car::arrow), since a GPS that stands still has no direction.
//
// Otherwise the map is north up, and it turns there by itself when the car slows or stops, and back when it moves
// off. The turn is smooth: the GPS gives a new heading once a second, and the map eases round to it (at most
// TURN_RATE degrees a second) instead of jumping.
#pragma once

#include "car.h"

namespace vtw {

constexpr double TURN_RATE_DEG_PER_SECOND = 120.0;   // the fastest the map turns
constexpr double TURN_SETTLE_PER_SECOND = 2.5;       // each second it closes this share of what is left to turn (it eases)
constexpr double ENGAGE_DEGREES = 2.0;               // a map that has settled ignores a direction closer than this (a GPS wobbles a degree or
                                                     // so, and turning the map redraws all of it); it starts turning when the direction is further off

// The heading that should be up now: the car's direction while heading up is on, the map follows the car, and the car
// is moving with a direction; otherwise 0 (north up).
double heading_target(bool heading_up, bool following, const Car& car);

// The shortest turn from one direction to another, in degrees, -180 to 180 (positive is clockwise).
double shortest_turn(double from_deg, double to_deg);

// Eases the heading that is up toward its target, the short way round. A settled map holds still for changes of
// under ENGAGE_DEGREES (so a wobbling GPS does not keep it redrawing), but once it has started to turn it goes all the
// way to the target, and north up (a target of 0) is always reached exactly.
class HeadingSmoother {
public:
    // dt seconds have passed; returns the heading to use now, 0 up to 360. It lands exactly on the target (so a map
    // that has stopped turning is the same from one frame to the next, and nothing is worked out again).
    double update(double target_deg, double dt);
    double heading() const { return heading_; }

private:
    double heading_ = 0;
    bool turning_ = false;                      // it has started to turn and has not landed yet
};

}  // namespace vtw
