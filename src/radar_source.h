// RadarSource: how to ask NOAA's radar map service for a picture of an area. The service is
// a standard web map server (WMS): give it a box in latitude and longitude and a size in
// pixels, and it draws the radar for that box (the national 1 km composite, which is updated
// about every 5 minutes) as a PNG with a transparent background.
#pragma once

#include <string>

#include "view.h"

namespace vtw {

// The radar's own resolution is about 1 km per pixel, so a picture needs no more than that.
constexpr double RADAR_KM_PER_PIXEL = 1.0;
constexpr int MIN_PICTURE_SIDE = 64;
constexpr int MAX_PICTURE_SIDE = 1500;         // keeps the answer small, however far out the view is

// How many pixels wide and high a picture of this area should be: about one per kilometre,
// within the limits above.
void radar_picture_size(const GeoBox& box, int& width, int& height);

// The web address that asks for a picture of the area. The box is clipped to the real earth.
std::string radar_url(const GeoBox& box);

}  // namespace vtw
