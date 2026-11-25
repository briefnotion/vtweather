// A temporary stand-in for the map: a graticule (lines of latitude and longitude) and a
// marker for a place. It is only here to show that the View puts things in the right place;
// the real map layers have replaced most of it.
#pragma once

#include "cell_grid.h"
#include "layers.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

void draw_graticule(const View& view, const CellGrid& grid, vtscreen::Picture& picture, LayerBlend& blend);
void draw_marker(const View& view, GeoPoint place, vtscreen::Picture& picture);

}  // namespace vtw
