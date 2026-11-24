// Drawing the ground: water, land and coast, kept dim (so rain and everything else stands out)
// and readable without colour: water is left blank, land is a faint scatter of dots and the
// coast is a brighter line. In colour, water is a dark blue background and land a dark green one.
#pragma once

#include "ground_map.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

void draw_ground(const GroundMap& ground, vtscreen::Picture& picture);

}  // namespace vtw
