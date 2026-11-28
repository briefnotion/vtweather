// Drawing the ground. Water is navy letters on a navy background: in colour, a plain navy block; without
// colour, faint dots that hardly move. Land has no colour and no letters (it shows the terminal's own
// background). Rivers, roads, state borders, country borders and the coast are lines of letters on it,
// kept dim (so the rain stands out) and each a step brighter than the last, so they differ even
// without colour: blue (rivers), amber (roads, the major highways brighter), violet (states), pink
// (countries) and tan (the coast).
#pragma once

#include "ground_map.h"
#include "layers.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

// The lines go in through the LayerBlend (each is a layer), so where they overlap each other, or the
// grid or the rain, the cell shows it (see layers.h).
// With lines = false only the water is drawn: the coast, borders, rivers and roads are drawn another way (the pen map).
void draw_ground(const GroundMap& ground, vtscreen::Picture& picture, LayerBlend& blend, bool lines = true);

}  // namespace vtw
