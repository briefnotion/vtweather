// Drawing the city names: a dot at the place and the name beside it, as text cells so they are
// always readable, in a colour that follows the size of the city (also fine without colour).
#pragma once

#include "place_labels.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

void draw_places(const PlaceLabels& places, vtscreen::Picture& picture);

}  // namespace vtw
