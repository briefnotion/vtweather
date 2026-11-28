// Drawing the rain's contour lines (see rain_contours.h) with the pen, like the lines of a topo map: one set of lines for each
// band of strength, light first and severe last, so the heavier lines are on top.
//
// In colour each band is a solid line in its own colour (green, yellow, orange, magenta); the cells under the rain also get a dim
// tint of the same colour (see draw_rain_tints), which is the fill. Without colour (a monochrome screen) there is no fill at all, so
// that nothing hides the map's borders, and the bands are told apart by how the line is drawn: dotted, dashed, solid, and doubled.
// The map's own lines are drawn after the rain, on top of it.
//
// Like the map's lines they are kept out of every cell that has text in it, and cut where they would cross one.
#pragma once

#include <vector>

#include "cell_grid.h"
#include "cell_point.h"
#include "pen_map.h"
#include "radar_frame.h"
#include "rain_contours.h"
#include "rain_style.h"
#include "vtpen_drawing.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

// Cut a line (any units) into dashes: on for `on`, off for `off`, starting with a dash. Each piece has at least two points.
std::vector<std::vector<CellPoint>> dash_line(const std::vector<CellPoint>& line, double on, double off);

// A line beside this one, `distance` away to the left of the way it goes (to the right if negative). A bend is followed by a mitre
// that is never more than twice as far out as the distance.
std::vector<CellPoint> offset_line(const std::vector<CellPoint>& line, double distance);

// The lengths the styles use, in pixels, for cells this wide.
struct DashSizes {
    double dot_on, dot_off, dash_on, dash_off, double_gap;
};
DashSizes dash_sizes(int cell_w);

// Draw the bands onto a drawing the size of the map in pixels (cols * cell_w by rows * cell_h).
void draw_rain_lines(vtpen::Drawing& drawing, const RainContours& contours, int cols, int rows, int cell_w, int cell_h,
                     const Blocked& blocked, bool white);

// Colour only: the background of each cell with rain in it is tinted by the band of its strongest echo. Letters and colours
// already on the cell stay. Not drawn without colour.
void draw_rain_tints(const CellGrid& grid, const RadarFrame& frame, vtscreen::Picture& picture);

}  // namespace vtw
