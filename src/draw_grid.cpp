#include "draw_grid.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace vtw {

namespace {

constexpr double LINE_BRIGHTNESS = 0.28;
constexpr double CROSSING_BRIGHTNESS = 0.5;
constexpr int MIN_CELLS_BETWEEN_LINES = 12;

// 1, 2 or 5 times a power of ten, the smallest that is at least x.
double nice_step(double x) {
    const double power = std::pow(10.0, std::floor(std::log10(x)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * power >= x) return m * power;
    return 10 * power;
}

std::string degrees_text(double value, char positive, char negative, int decimals) {
    char text[32];
    std::snprintf(text, sizeof text, "%.*f%c", decimals, std::fabs(value),
                  value >= 0 ? positive : negative);
    return text;
}

// Does a line of this grid (multiples of step) pass through [low, high]?
bool line_between(double low, double high, double step) {
    if (high - low > 180) return false;                      // wrapped round the date line
    return std::floor(low / step) != std::floor(high / step);
}

}  // namespace

void draw_graticule(const View& view, vtscreen::Picture& picture) {
    const int cols = view.cols(), rows = view.rows();

    // the earth under every cell corner
    std::vector<GeoPoint> corner((size_t)(cols + 1) * (rows + 1));
    for (int r = 0; r <= rows; r++)
        for (int c = 0; c <= cols; c++) corner[(size_t)r * (cols + 1) + c] = view.at(c, r);

    const double lon_per_col = std::fabs(view.at(cols / 2.0 + 1, rows / 2.0).lon -
                                         view.at(cols / 2.0, rows / 2.0).lon);
    const double step = nice_step(MIN_CELLS_BETWEEN_LINES * lon_per_col);
    const int decimals = std::max(0, -(int)std::floor(std::log10(step) + 1e-9));

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const GeoPoint& a = corner[(size_t)r * (cols + 1) + c];
            const GeoPoint& b = corner[(size_t)r * (cols + 1) + c + 1];
            const GeoPoint& d = corner[(size_t)(r + 1) * (cols + 1) + c];
            const GeoPoint& e = corner[(size_t)(r + 1) * (cols + 1) + c + 1];
            const double lat_low = std::min({a.lat, b.lat, d.lat, e.lat});
            const double lat_high = std::max({a.lat, b.lat, d.lat, e.lat});
            const double lon_low = std::min({a.lon, b.lon, d.lon, e.lon});
            const double lon_high = std::max({a.lon, b.lon, d.lon, e.lon});
            const bool lat_line = line_between(lat_low, lat_high, step);
            const bool lon_line = line_between(lon_low, lon_high, step);
            if (!lat_line && !lon_line) continue;

            vtscreen::PicCell& cell = picture.at(r, c);
            cell.lum = lat_line && lon_line ? CROSSING_BRIGHTNESS : LINE_BRIGHTNESS;
            cell.r = 90; cell.g = 135; cell.b = 175;

            // labels: latitudes down the left edge, longitudes along the bottom
            if (lat_line && c == 0)
                picture.text(r, 0, degrees_text(std::floor(lat_high / step) * step, 'N', 'S', decimals),
                             110, 160, 205);
            if (lon_line && r == rows - 1)
                picture.text(r, c + 1, degrees_text(std::floor(lon_high / step) * step, 'E', 'W', decimals),
                             110, 160, 205);
        }
    }
}

void draw_marker(const View& view, GeoPoint place, vtscreen::Picture& picture) {
    double col, row;
    view.to_cell(place, col, row);
    if (col < 0 || row < 0 || col >= view.cols() || row >= view.rows()) return;
    picture.text((int)row, (int)col, "+", 255, 220, 90);
}

void draw_readout(const View& view, const Settings& settings, const std::string& notes,
                  vtscreen::Picture& picture) {
    char line[160];
    int row = 0;
    picture.text(row++, 1, settings.place_name);
    const GeoPoint c = view.centre();
    std::snprintf(line, sizeof line, "centre %s %s", degrees_text(c.lat, 'N', 'S', 3).c_str(),
                  degrees_text(c.lon, 'E', 'W', 3).c_str());
    picture.text(row++, 1, line);
    std::snprintf(line, sizeof line, "%.0f km across, %.2f km per column", view.zoom_km(),
                  view.km_per_col());
    picture.text(row++, 1, line);
    picture.text(row++, 1, "settings: " + settings.source);

    size_t start = 0;
    while (start < notes.size()) {                           // one amber line per note
        size_t end = notes.find('\n', start);
        if (end == std::string::npos) end = notes.size();
        picture.text(row++, 1, notes.substr(start, end - start), 255, 190, 80);
        start = end + 1;
    }
}

}  // namespace vtw
