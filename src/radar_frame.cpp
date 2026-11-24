#include "radar_frame.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace vtw {

namespace {

// The colour scale, from the faintest echo to the strongest.
const std::vector<ScaleStop> SCALE = {
    {210, 212, 180, 5},                                     // pale: the faintest echoes
    {170, 177, 180, 6},
    {128, 140, 176, 8},                                     // grey-blue
    {99, 118, 168, 10},
    {67, 94, 159, 12},
    {72, 110, 169, 13},                                     // blue
    {94, 173, 207, 15},
    {89, 189, 189, 17},                                     // cyan
    {82, 214, 162, 19},
    {62, 214, 119, 20},                                     // light green: light rain
    {14, 214, 20, 22},
    {13, 198, 19, 25},
    {12, 152, 16, 28},                                      // green
    {9, 94, 9, 31},                                         // dark green: moderate rain
    {255, 211, 0, 35},                                      // yellow: heavy rain
    {255, 192, 0, 40},
    {255, 124, 0, 45},                                      // orange
    {255, 0, 0, 50},                                        // red: very heavy rain
    {190, 0, 0, 55},
    {255, 0, 255, 65},                                      // magenta: hail
    {153, 85, 201, 70},                                     // purple
    {255, 255, 255, 75},                                    // white: the strongest
};
const int STOPS = (int)SCALE.size();

// How far, in colour distance, a pixel may be from the scale and still count as rain. The
// colours of real pictures are within 20; the blur between two colours is further.
constexpr double ACCEPT_DISTANCE = 30.0;
constexpr int MIN_ALPHA = 128;

// The estimate for a colour that is not on the scale but is clearly a strong echo (warm
// and saturated), by its hue. 0 if it is not one.
double strong_by_hue(int r, int g, int b) {
    const int high = std::max({r, g, b}), low = std::min({r, g, b});
    if (high < 90 || high - low < 0.5 * high) return 0;                // dull or dark: not one
    double hue;
    const double d = high - low;
    if (high == r) hue = 60 * std::fmod((g - b) / d, 6.0);
    else if (high == g) hue = 60 * ((b - r) / d + 2);
    else hue = 60 * ((r - g) / d + 4);
    if (hue < 0) hue += 360;
    if (hue >= 50 && hue <= 70) return 35;                              // yellow
    if (hue >= 35 && hue < 50) return 40;                               // gold
    if (hue >= 20 && hue < 35) return 45;                               // orange
    if (hue < 20 || hue >= 340) return high < 200 ? 55 : 50;            // red, dark red
    if (hue >= 280 && hue < 340) return 65;                             // magenta
    if (hue >= 240 && hue < 280) return 70;                             // purple
    return 0;
}

double estimate(int r, int g, int b) {
    double best_distance = 1e9, best_dbz = 0;
    for (int i = 0; i + 1 < STOPS; i++) {
        const ScaleStop& a = SCALE[i];
        const ScaleStop& c = SCALE[i + 1];
        const double ab[3] = {double(c.r - a.r), double(c.g - a.g), double(c.b - a.b)};
        const double ap[3] = {double(r - a.r), double(g - a.g), double(b - a.b)};
        const double length2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
        double t = (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / length2;     // along the piece
        t = std::min(std::max(t, 0.0), 1.0);
        const double dr = ap[0] - t * ab[0], dg = ap[1] - t * ab[1], db = ap[2] - t * ab[2];
        const double distance = std::sqrt(dr * dr + dg * dg + db * db);
        if (distance < best_distance) { best_distance = distance; best_dbz = a.dbz + t * (c.dbz - a.dbz); }
    }
    if (best_distance <= ACCEPT_DISTANCE) return best_dbz;
    return strong_by_hue(r, g, b);
}

}  // namespace

const std::vector<ScaleStop>& colour_scale() { return SCALE; }

uint8_t dbz_from_colour(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a < MIN_ALPHA) return 0;
    const double dbz = estimate(r, g, b);
    return dbz <= 0 ? 0 : (uint8_t)std::lround(dbz);
}

uint8_t RadarFrame::value_at(GeoPoint place) const {
    return strongest_in(GeoBox{place.lat, place.lon, place.lat, place.lon});
}

uint8_t RadarFrame::strongest_in(const GeoBox& area) const {
    if (width <= 0 || height <= 0) return 0;
    if (area.east < box.west || area.west > box.east || area.north < box.south || area.south > box.north) return 0;
    const double pixel_w = (box.east - box.west) / width;
    const double pixel_h = (box.north - box.south) / height;
    // The pixels the area covers: from the one holding its west/north edge to the last one that
    // it reaches into (an area ending exactly on a pixel border does not take the next pixel).
    auto first = [](double start, double size) { return (int)std::floor(start / size); };
    auto last = [](double end, double size, int from) { return std::max(from, (int)std::ceil(end / size) - 1); };
    int x0 = first(area.west - box.west, pixel_w), y0 = first(box.north - area.north, pixel_h);
    int x1 = last(area.east - box.west, pixel_w, x0), y1 = last(box.north - area.south, pixel_h, y0);
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, width - 1); y1 = std::min(y1, height - 1);
    uint8_t strongest = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) strongest = std::max(strongest, at(x, y));
    return strongest;
}

bool make_radar_frame(const Image& image, const GeoBox& box, RadarFrame& frame, std::string& error) {
    frame = RadarFrame();
    if (image.width <= 0 || image.height <= 0 || image.rgba.size() != (size_t)image.width * image.height * 4) {
        error = "there is no picture to read";
        return false;
    }
    if (!(box.east > box.west) || !(box.north > box.south)) {
        error = "the area of the picture is empty";
        return false;
    }
    frame.box = box;
    frame.width = image.width;
    frame.height = image.height;
    frame.dbz.resize((size_t)image.width * image.height);

    std::unordered_map<uint32_t, uint8_t> known;        // a picture has only a few hundred colours
    for (size_t i = 0; i < frame.dbz.size(); i++) {
        const uint8_t* p = &image.rgba[i * 4];
        const uint32_t key = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        auto found = known.find(key);
        if (found == known.end()) found = known.emplace(key, dbz_from_colour(p[0], p[1], p[2], p[3])).first;
        frame.dbz[i] = found->second;
    }
    return true;
}

}  // namespace vtw
