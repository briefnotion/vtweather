#include "draw_ground.h"

namespace vtw {

namespace {

constexpr double LAND_BRIGHTNESS = 0.10;
constexpr double COAST_BRIGHTNESS = 0.26;

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
            switch (ground.at(col, row)) {
                case Ground::Water: set_ground(cell, 0.0, 0, 0, 0, 8, 22, 48); break;
                case Ground::Land: set_ground(cell, LAND_BRIGHTNESS, 60, 90, 55, 12, 24, 12); break;
                case Ground::Coast: set_ground(cell, COAST_BRIGHTNESS, 190, 170, 120, 12, 24, 12); break;
            }
        }
}

}  // namespace vtw
