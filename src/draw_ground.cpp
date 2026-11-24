#include "draw_ground.h"

namespace vtw {

namespace {

// Each kind of thing has its own brightness, a step apart, so that they can be told apart even
// without colour (and all stay below the rain, which starts at 0.36).
constexpr double LAND_BRIGHTNESS = 0.09;
constexpr double RIVER_BRIGHTNESS = 0.15;
constexpr double STATE_BRIGHTNESS = 0.20;
constexpr double COUNTRY_BRIGHTNESS = 0.26;
constexpr double COAST_BRIGHTNESS = 0.33;

void set_ground(vtscreen::PicCell& cell, double brightness, int r, int g, int b, int back_r, int back_g, int back_b) {
    cell.lum = brightness;
    cell.r = r; cell.g = g; cell.b = b;
    cell.alt_lum = 1.0;                              // the colour behind the letters
    cell.alt_r = back_r; cell.alt_g = back_g; cell.alt_b = back_b;
}

}  // namespace

void draw_ground(const GroundMap& ground, vtscreen::Picture& picture) {
    for (int row = 0; row < ground.rows(); row++)
        for (int col = 0; col < ground.cols(); col++) {
            vtscreen::PicCell& cell = picture.at(row, col);
            const Ground kind = ground.at(col, row);
            const uint8_t features = ground.features_at(col, row);
            if (kind == Ground::Water) {
                set_ground(cell, 0.0, 0, 0, 0, 8, 22, 48);
            } else if (features & FEATURE_COUNTRY) {                 // borders and rivers show on land and coast
                set_ground(cell, COUNTRY_BRIGHTNESS, 215, 120, 150, 12, 24, 12);
            } else if (features & FEATURE_STATE) {
                set_ground(cell, STATE_BRIGHTNESS, 150, 120, 175, 12, 24, 12);
            } else if (kind == Ground::Coast) {
                set_ground(cell, COAST_BRIGHTNESS, 190, 170, 120, 12, 24, 12);
            } else if (features & FEATURE_RIVER) {
                set_ground(cell, RIVER_BRIGHTNESS, 70, 140, 205, 12, 24, 12);
            } else {
                set_ground(cell, LAND_BRIGHTNESS, 60, 90, 55, 12, 24, 12);
            }
        }
}

}  // namespace vtw
