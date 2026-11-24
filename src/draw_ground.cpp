#include "draw_ground.h"

namespace vtw {

namespace {

// Each line has its own brightness, a step apart, so that they can be told apart even without colour
// (and all stay below the rain, which starts at 0.36).
constexpr double RIVER_BRIGHTNESS = 0.15;
constexpr double ROAD_BRIGHTNESS = 0.16;
constexpr double STATE_BRIGHTNESS = 0.20;
constexpr double HIGHWAY_BRIGHTNESS = 0.23;
constexpr double COUNTRY_BRIGHTNESS = 0.26;
constexpr double COAST_BRIGHTNESS = 0.33;

// Water: navy letters on a navy background, so in colour the letters cannot be seen, and the water
// is a plain navy block. Without colour the letters show: the brightness is the lowest that makes
// any (a hair above the first of the 16 steps, 1/16, where the group of faint dots starts), and the
// shimmer is as low as it can be without stopping altogether, one change of letter in about 80
// seconds, so the water barely moves.
constexpr double WATER_BRIGHTNESS = 0.07;
constexpr double WATER_SHIMMER = 0.001;
constexpr int WATER_R = 8, WATER_G = 22, WATER_B = 48;

// Land has no colour of its own and no letters: it is left as it is, showing the terminal's own
// background. Lines on the land are letters only, with no background colour; each kind is a layer.

void set_water(vtscreen::PicCell& cell) {
    cell.lum = WATER_BRIGHTNESS;
    cell.shimmer = WATER_SHIMMER;
    cell.r = WATER_R; cell.g = WATER_G; cell.b = WATER_B;                 // the letters,
    cell.alt_lum = 1.0;                                                   // and the colour behind them: the same
    cell.alt_r = WATER_R; cell.alt_g = WATER_G; cell.alt_b = WATER_B;
}

}  // namespace

void draw_ground(const GroundMap& ground, vtscreen::Picture& picture, LayerBlend& blend) {
    for (int row = 0; row < ground.rows(); row++)
        for (int col = 0; col < ground.cols(); col++) {
            vtscreen::PicCell& cell = picture.at(row, col);
            const Ground kind = ground.at(col, row);
            const uint8_t features = ground.features_at(col, row);
            if (kind == Ground::Water) {                             // borders, roads and rivers do not run over water
                set_water(cell);
                continue;
            }
            // Every line in the cell is a layer of its own (the two kinds of border, and the two of road, are one
            // layer each: the brighter kind is the one drawn).
            if (kind == Ground::Coast) blend.add(row, col, Layer::Coast, COAST_BRIGHTNESS, 190, 170, 120);
            if (features & FEATURE_COUNTRY) blend.add(row, col, Layer::Border, COUNTRY_BRIGHTNESS, 215, 120, 150);
            else if (features & FEATURE_STATE) blend.add(row, col, Layer::Border, STATE_BRIGHTNESS, 150, 120, 175);
            if (features & FEATURE_HIGHWAY) blend.add(row, col, Layer::Road, HIGHWAY_BRIGHTNESS, 230, 180, 85);
            else if (features & FEATURE_ROAD) blend.add(row, col, Layer::Road, ROAD_BRIGHTNESS, 165, 135, 80);
            if (features & FEATURE_RIVER) blend.add(row, col, Layer::River, RIVER_BRIGHTNESS, 70, 140, 205);
        }
}

}  // namespace vtw
