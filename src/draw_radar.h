// Drawing the rain: every screen cell takes the strongest echo that falls in it, and shows it
// as letters. Stronger rain is brighter (so it reads in plain letters with no colour at all),
// and in colour follows the usual weather scale: pale blue, green, yellow, orange, red, magenta.
// On its own the rain holds still; where it overlaps other layers the cell shimmers (see layers.h).
#pragma once

#include "cell_grid.h"
#include "layers.h"
#include "radar_frame.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

// How one strength looks.
struct RainLook {
    double brightness;        // 0..1, always above the map's dim lines
    int r, g, b;
};

RainLook rain_look(int dbz);

void draw_radar(const CellGrid& grid, const RadarFrame& frame, LayerBlend& blend);

// A small key to the rain, in the lower right: a strip from light to heavy rain made of the same
// cells the rain is drawn with (so it shows the letters and colours as they really look), and a
// line of words over it. Left out when the screen is too small.
constexpr int LEGEND_WIDTH = 28;
constexpr int LEGEND_MIN_COLS = 70, LEGEND_MIN_ROWS = 16;
void draw_legend(vtscreen::Picture& picture);

}  // namespace vtw
