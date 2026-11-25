// Car: where the car is, for the map, worked out from what the GPS says (see gps_feed.h).
//
// A GPS is not steady when it is standing still: the position wanders by tens of metres, and the
// direction it reports is noise. So there are two rules:
//
//   - Above a speed (moving_mph, 5 by default) the car is "moving": the marker is the live position,
//     and an arrow shows the direction of travel. Below it the marker is a plain + and the position is
//     held where it was, so a car at a light stays put and a map that follows it does not crawl about.
//   - There is a small gap so that it does not flicker around the limit: it turns on at moving_mph and
//     off again only below a bit under that (OFF_FRACTION of it, 3 mph for 5).
//
// A position is only shown while there is a fix. When a fix comes back after not having one (or the
// very first one), the position starts from the live fix, whatever the speed: a held position from
// long ago is no use.
#pragma once

#include <string>

#include "gps_feed.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

constexpr double DEFAULT_MOVING_MPH = 5.0;
constexpr double OFF_FRACTION = 0.6;            // moving turns off below this part of moving_mph
constexpr double KMPH_TO_MPH = 0.621371;
constexpr double METERS_TO_FEET = 3.28084;

class Car {
public:
    explicit Car(double moving_mph = DEFAULT_MOVING_MPH) : moving_mph_(moving_mph) {}

    // Call every frame with the GPS state and its newest reading.
    void update(GpsState state, const GpsFix& fix);

    bool visible() const { return visible_; }       // there is a fix: the marker is drawn
    GeoPoint position() const { return position_; } // live while moving, held when slow
    bool moving() const { return moving_; }
    bool arrow() const { return visible_ && moving_ && heading_valid_; }   // moving, with a direction to show
    double heading() const { return heading_; }     // degrees, 0 up to 360, the direction of travel
    double speed_mph() const { return speed_mph_; } // the live speed, even when held

private:
    double moving_mph_;
    bool visible_ = false;
    bool had_fix_ = false;
    bool moving_ = false;
    bool heading_valid_ = false;
    GeoPoint position_;
    double heading_ = 0;
    double speed_mph_ = 0;
};

// The arrow for a direction of travel (up, up-right, right ... in eight steps), the direction taken
// relative to where up is on the screen (the view's heading, 0 for north up).
const char* arrow_glyph(double heading_deg, double view_heading_deg);

// The name of a direction in eight steps: N, NE, E, SE, S, SW, W, NW.
const char* compass_name(double heading_deg);

// Keep the map centred on the car while following: true if the view was moved. Nothing happens when not
// following, or when there is no fix (the map stays where it is until there is one). The zoom is left alone.
bool follow_car(bool following, const Car& car, View& view);

// Draw the car where it is: an arrow while moving, a + when slow or stopped. Nothing if there is no
// fix or the car is off the screen.
void draw_car(const View& view, const Car& car, vtscreen::Picture& picture);

}  // namespace vtw
