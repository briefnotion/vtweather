// Rain as contour lines, like a topo map. The radar picture is turned into four bands of strength (light,
// moderate, heavy, severe); the edge of each band is traced as lines, which the pen draws (see rain_pen.h). Nothing here
// draws: it works on numbers, in cells.
//
// The strengths are first laid out on a lattice over the screen, finer than a cell (so the lines are not blocky): where the
// radar's own pixels are bigger than a lattice step the strength between them is smoothed (bilinear), where a step covers
// many pixels (a zoomed-out view) it takes the strongest, so a storm's core is never averaged away. One 3 x 3 mean takes the
// speckle out. Each band's edge is then followed through the lattice (marching squares) into lines, which are simplified;
// specks too small to read are dropped.
#pragma once

#include <vector>

#include "cell_point.h"
#include "radar_frame.h"
#include "view.h"

namespace vtw {

constexpr int RAIN_BANDS = 4;                                   // light, moderate, heavy, severe
constexpr double RAIN_BAND_MIN_DBZ[RAIN_BANDS] = {15, 30, 40, 50};
// The band of a strength: the highest whose limit it reaches, or -1 for none.
int rain_band_of(double dbz);

// A line followed through the lattice, in cells. A closed one has no repeated end point.
struct ContourLine {
    std::vector<CellPoint> points;
    bool closed = false;
};

// Strengths on a lattice over the screen: node (i, j) is at cell position (i * step_col, j * step_row), the first at (0, 0)
// and the last at (cols, rows).
struct RainField {
    int nx = 0, ny = 0;
    double step_col = 0, step_row = 0;
    std::vector<float> value;                                   // nx * ny, row by row
    float at(int i, int j) const { return value[(size_t)j * nx + i]; }
};

constexpr double FIELD_STEP_COLS = 0.5, FIELD_STEP_ROWS = 0.25;      // about square on screen when a cell is twice as tall as wide

// The strengths under a view. Empty (nx = 0) if there is no frame or no view. Where the radar's pixels are bigger than a lattice step
// the strengths between pixel centres are smoothed, unless peaks is true: then every node takes the strongest pixel it touches, so
// that a one pixel core is not smoothed away.
RainField sample_rain_field(const RadarFrame& frame, const View& view, bool peaks = false);

// One 3 x 3 mean (the edge nodes use their own value for what is outside).
void smooth_field(RainField& field);

// The lines where the field crosses a level (nodes at or above it are inside).
std::vector<ContourLine> trace_contours(const RainField& field, double level);

// The corners cut once (Chaikin): the ends of an open line stay. Takes the staircase out of a line followed through a coarse lattice.
ContourLine round_contour(const ContourLine& line);

// Fewer points (no point moves further than SIMPLIFY_TOLERANCE columns), and no specks: a closed line enclosing less than
// min_loop_area, or an open one shorter than min_length, is dropped (both in columns, squared for the area). aspect is how many times
// taller a cell is than wide. With marker_radius above 0 a small closed line is not dropped but made into a small circle of that
// radius (in columns) round its middle, so that a small strong core can still be seen.
constexpr double SIMPLIFY_TOLERANCE = 0.10;
std::vector<ContourLine> tidy_contours(const std::vector<ContourLine>& lines, double aspect, double min_loop_area, double min_length,
                                       double marker_radius = 0);

// What counts as a speck for each band. The light band is drawn from the smoothed field and loses anything smaller than about a
// cell; the others come from the field as it is (every node the strongest pixel it touches), and keep their small cores, which
// matter more than a tidy line.
constexpr double MIN_LOOP_AREA_BY_BAND[RAIN_BANDS] = {1.5, 0.8, 0.12, 0.04};
constexpr double MIN_LINE_LENGTH_BY_BAND[RAIN_BANDS] = {2.0, 1.5, 0.5, 0.3};
constexpr double MARKER_RADIUS_BY_BAND[RAIN_BANDS] = {0, 0.3, 0.3, 0.3};        // moderate, heavy and severe cores are never lost: a small circle stands for one

struct RainContours {
    std::vector<ContourLine> band[RAIN_BANDS];
    bool empty() const {
        for (const auto& b : band) if (!b.empty()) return false;
        return true;
    }
};

// Everything above for a view.
RainContours make_rain_contours(const RadarFrame& frame, const View& view);

}  // namespace vtw
