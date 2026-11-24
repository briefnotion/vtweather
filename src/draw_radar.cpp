#include "draw_radar.h"

#include <algorithm>
#include <cmath>

namespace vtw {

namespace {

struct Colour {
    double dbz;
    int r, g, b;
};

// The rain's colours by strength, in the style of the weather service's own scale.
const Colour COLOURS[] = {
    {5, 100, 140, 190},     // faint: pale blue
    {15, 80, 180, 220},
    {20, 70, 210, 130},     // light rain: green
    {30, 60, 190, 60},
    {35, 240, 220, 50},     // heavy: yellow
    {40, 245, 170, 40},
    {45, 245, 120, 30},     // orange
    {50, 235, 50, 40},      // very heavy: red
    {60, 220, 40, 200},     // magenta
    {70, 240, 230, 250},    // the strongest
};
constexpr int COLOUR_COUNT = (int)(sizeof COLOURS / sizeof COLOURS[0]);

constexpr double FAINTEST_DBZ = 5, STRONGEST_DBZ = 60;
constexpr double FAINTEST_BRIGHTNESS = 0.36, BRIGHTNESS_RANGE = 0.60;     // the map's lines are 0.28

}  // namespace

RainLook rain_look(int dbz) {
    RainLook look{};
    const double t = std::min(std::max((dbz - FAINTEST_DBZ) / (STRONGEST_DBZ - FAINTEST_DBZ), 0.0), 1.0);
    look.brightness = FAINTEST_BRIGHTNESS + BRIGHTNESS_RANGE * std::pow(t, 0.8);
    look.shimmer = std::min(0.5 + dbz / 25.0, 2.4);

    const double d = std::min(std::max<double>(dbz, COLOURS[0].dbz), COLOURS[COLOUR_COUNT - 1].dbz);
    int i = 0;
    while (i + 2 < COLOUR_COUNT && d > COLOURS[i + 1].dbz) i++;
    const Colour& a = COLOURS[i];
    const Colour& b = COLOURS[i + 1];
    const double u = (d - a.dbz) / (b.dbz - a.dbz);
    look.r = (int)std::lround(a.r + u * (b.r - a.r));
    look.g = (int)std::lround(a.g + u * (b.g - a.g));
    look.b = (int)std::lround(a.b + u * (b.b - a.b));
    return look;
}

void draw_radar(const CellGrid& grid, const RadarFrame& frame, vtscreen::Picture& picture) {
    if (frame.width <= 0) return;
    for (int row = 0; row < grid.rows(); row++) {
        for (int col = 0; col < grid.cols(); col++) {
            const uint8_t dbz = frame.strongest_in(grid.box(col, row));
            if (dbz == 0) continue;
            const RainLook look = rain_look(dbz);
            vtscreen::PicCell& cell = picture.at(row, col);
            cell.lum = look.brightness;
            cell.r = look.r; cell.g = look.g; cell.b = look.b;
            cell.shimmer = look.shimmer;
        }
    }
}

}  // namespace vtw
