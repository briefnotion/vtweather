#include "rain_style.h"

#include <algorithm>
#include <cmath>

#include "rain_contours.h"

namespace vtw {

namespace {

constexpr uint8_t BAND_COLOUR[RAIN_BANDS][3] = {{70, 210, 130}, {240, 220, 50}, {245, 120, 30}, {220, 40, 200}};
constexpr double TINT_SHARE = 0.30;                       // how much of a band's colour is the tint under it

}  // namespace

RainStyle rain_style(int band, bool white) {
    band = std::max(0, std::min(RAIN_BANDS - 1, band));
    RainStyle style;
    if (white) {
        style.r = style.g = style.b = 255;
        const LineStyle by_band[RAIN_BANDS] = {LineStyle::Dotted, LineStyle::Dashed, LineStyle::Solid, LineStyle::Doubled};
        style.line = by_band[band];
    } else {
        style.line = LineStyle::Solid;
        style.r = BAND_COLOUR[band][0]; style.g = BAND_COLOUR[band][1]; style.b = BAND_COLOUR[band][2];
    }
    return style;
}

RainTint rain_tint(int band) {
    band = std::max(0, std::min(RAIN_BANDS - 1, band));
    return {(int)std::lround(BAND_COLOUR[band][0] * TINT_SHARE), (int)std::lround(BAND_COLOUR[band][1] * TINT_SHARE),
            (int)std::lround(BAND_COLOUR[band][2] * TINT_SHARE)};
}

}  // namespace vtw
