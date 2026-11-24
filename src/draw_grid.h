// A temporary stand-in for the map: a graticule (lines of latitude and longitude), a
// marker for a place, and a readout. It is only here to show that the View
// puts things in the right place; the real map layers will replace it.
#pragma once

#include "cell_grid.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

void draw_graticule(const View& view, const CellGrid& grid, vtscreen::Picture& picture);
void draw_marker(const View& view, GeoPoint place, vtscreen::Picture& picture);
void draw_readout(const View& view, const Settings& settings, const std::string& notes,
                  const std::string& radar_status, vtscreen::Picture& picture);

}  // namespace vtw
