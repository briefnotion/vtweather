// How the rain's contour lines look (see rain_pen.h): in colour each band is a solid line in its own colour, with a dim tint of it
// under the rain's cells; without colour (a monochrome screen) there is no fill and the bands are white lines in four styles.
#pragma once

#include <cstdint>

namespace vtw {

enum class LineStyle { Dotted, Dashed, Solid, Doubled };

struct RainStyle {
    LineStyle line = LineStyle::Solid;
    uint8_t r = 255, g = 255, b = 255;
};

// How a band's line looks: in colour solid and coloured, without colour white and in the band's style of line.
RainStyle rain_style(int band, bool white);

// The dim colour a band tints the cells under it (colour only).
struct RainTint {
    int r, g, b;
};
RainTint rain_tint(int band);

}  // namespace vtw
