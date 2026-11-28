// What the terminal can draw the pen map with, found by asking it (vtpen's probe) once at start-up:
// ReGIS (small and quick, but only as big as the terminal's ReGIS screen: it has to cover the whole map), else Sixel
// (pixels, any size), else nothing, and then the map is letters only, as it was before the pen.
#pragma once

#include "vtpen_probe.h"

namespace vtw {

enum class PenKind { None, Sixel, Regis };

struct PenSupport {
    PenKind kind = PenKind::None;
    int cell_w = 0, cell_h = 0;          // one character cell, in pixels
    int surface_w = 0, surface_h = 0;    // the ReGIS screen, in pixels (ReGIS only)
};

// What to use, from what the terminal said, for a map cols x rows cells big: ReGIS if it lists ReGIS, says how big a cell
// is, and its ReGIS screen (the biggest picture it allows, at most the window) covers the whole map; else Sixel if it
// can draw it and says how big a cell is; else none.
PenSupport pen_support_from(const vtpen::Capabilities& capabilities, int map_cols, int map_rows);

// Can the pen map be drawn over a map of this many cells? Always with Sixel; with ReGIS only if its screen covers the
// map (a window made bigger since start-up may not be covered any more); never with none.
bool pen_covers(const PenSupport& support, int map_cols, int map_rows);

// Ask the terminal on standard input and output (about a second at most). None if they are not a terminal, if it does not
// answer, or on a system where vtpen's probe is not available (Windows).
PenSupport detect_pen_support(int map_cols, int map_rows);

// Tell vtscreen: set_image_support for Sixel, set_regis_support for ReGIS. Call before the Display is opened.
void apply_pen_support(const PenSupport& support);

}  // namespace vtw
