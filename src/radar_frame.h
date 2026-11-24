// RadarFrame: one radar picture turned into numbers. The radar service draws rain in colours;
// here each pixel becomes an estimated reflectivity in dBZ (0 = no rain), so that the rest of
// the program works with strength, never with colours, and can draw it in any style, in colour
// or not.
//
// How strong is a colour? NOAA's radar uses the usual weather-service colour scale: pale grey
// and blue for the faintest echoes, then cyan, greens, yellow, orange, red and on to magenta
// and purple. The program holds that scale as a list of known colours with a reflectivity for
// each, and a pixel gets the value of the nearest point along it. The dBZ values are estimates
// (the service publishes no table), good for telling light rain from heavy, not for measuring.
// The colours above dark green were taken from a few real pictures; the strongest end (reds
// and beyond) is the standard scale, and colours there that are not on the list are placed by
// their hue, so heavy rain is never dropped.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "png.h"
#include "view.h"

namespace vtw {

// One known colour of the scale and the reflectivity it stands for.
struct ScaleStop {
    int r, g, b;
    double dbz;
};

// The scale itself, from the faintest echo to the strongest (for a legend, and for tests).
const std::vector<ScaleStop>& colour_scale();

// Estimated reflectivity of one pixel colour, in dBZ, 0 when it is not rain: transparent,
// mostly transparent, or a colour nowhere near the scale (such as the blur between two colours
// where the service has shrunk a picture).
uint8_t dbz_from_colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

struct RadarFrame {
    GeoBox box;                        // the area the picture covers, edge to edge
    int width = 0, height = 0;
    std::vector<uint8_t> dbz;          // width * height, row by row from the top; 0 = no rain

    uint8_t at(int x, int y) const { return dbz[(size_t)y * width + x]; }

    // The strength at a place (the nearest pixel); 0 outside the picture.
    uint8_t value_at(GeoPoint place) const;

    // The strongest echo in an area. A screen cell that covers many pixels (a zoomed-out view)
    // takes the strongest one, so a storm's core is never averaged away. 0 outside the picture.
    uint8_t strongest_in(const GeoBox& area) const;
};

// Make a frame from a decoded picture of the given area. The picture must be the plain
// rectangular map the service returns for a box in latitude and longitude.
bool make_radar_frame(const Image& image, const GeoBox& box, RadarFrame& frame, std::string& error);

}  // namespace vtw
