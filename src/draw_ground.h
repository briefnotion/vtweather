// Drawing the ground: water, land and coast, with rivers and borders, kept dim (so rain and
// everything else stands out) and readable without colour: water is left blank, land is a faint
// scatter of dots, and rivers, roads, state borders, country borders and the coast are lines, each
// a step brighter than the last. In colour, water is a dark blue background, land a dark green one,
// and the lines are blue (rivers), amber (roads, the major highways brighter), violet (states), pink
// (countries) and tan (the coast).
#pragma once

#include "ground_map.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

void draw_ground(const GroundMap& ground, vtscreen::Picture& picture);

}  // namespace vtw
