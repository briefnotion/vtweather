#include "car.h"

#include <cmath>

namespace vtw {

namespace {

const double CAR_R = 255, CAR_G = 255, CAR_B = 255;        // the car is white, the home + is yellow
const double BACK_R = 10, BACK_G = 14, BACK_B = 22;        // behind it, the dark of the cards, so it reads over the rain

// An angle brought to 0 up to 360.
double wrapped(double degrees) {
    double a = std::fmod(degrees, 360.0);
    if (a < 0) a += 360.0;
    return a >= 360.0 ? 0.0 : a;
}

// Which of the eight 45 degree steps an angle is in (0 = up or north).
int eighth(double degrees) { return (int)std::floor((wrapped(degrees) + 22.5) / 45.0) % 8; }

}  // namespace

void Car::update(GpsState state, const GpsFix& fix) {
    if (state != GpsState::Fix) {
        visible_ = false;                              // no fix: nothing to show, and nothing to hold on to
        had_fix_ = false;
        moving_ = false;
        return;
    }
    speed_mph_ = fix.speed_kmph * KMPH_TO_MPH;
    // with a gap: it turns on at the limit and off below a part of it
    if (!moving_ && speed_mph_ >= moving_mph_) moving_ = true;
    else if (moving_ && speed_mph_ < moving_mph_ * OFF_FRACTION) moving_ = false;

    if (!had_fix_ || moving_) {                        // the first fix, or one that is moving: the live position
        position_ = GeoPoint{fix.lat, fix.lon};
    }                                                  // otherwise it is held
    heading_ = wrapped(fix.heading_deg);
    heading_valid_ = fix.heading_valid;
    visible_ = true;
    had_fix_ = true;
}

const char* arrow_glyph(double heading_deg, double view_heading_deg) {
    static const char* const ARROWS[8] = {"↑", "↗", "→", "↘", "↓", "↙", "←", "↖"};
    return ARROWS[eighth(heading_deg - view_heading_deg)];
}

const char* compass_name(double heading_deg) {
    static const char* const NAMES[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return NAMES[eighth(heading_deg)];
}

bool follow_car(bool following, const Car& car, View& view) {
    if (!following || !car.visible()) return false;
    view.set_centre(car.position());
    return true;
}

void draw_car(const View& view, const Car& car, vtscreen::Picture& picture) {
    if (!car.visible()) return;
    double col, row;
    view.to_cell(car.position(), col, row);
    if (col < 0 || row < 0 || col >= view.cols() || row >= view.rows()) return;
    const int c = (int)col, r = (int)row;
    if (r >= picture.bar_row() || c >= picture.cols) return;
    picture.text(r, c, car.arrow() ? arrow_glyph(car.heading(), view.heading()) : "+", CAR_R, CAR_G, CAR_B);
    vtscreen::PicCell& cell = picture.at(r, c);
    cell.alt_lum = 1;
    cell.alt_r = BACK_R; cell.alt_g = BACK_G; cell.alt_b = BACK_B;
}

}  // namespace vtw
