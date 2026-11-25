#include "draw_grid.h"

#include <cmath>
#include <cstdio>

namespace vtw {

namespace {

constexpr double KM_PER_DEGREE = 111.32;           // of latitude, or of longitude at the equator
constexpr double PI = 3.14159265358979323846;
constexpr double LINE_BRIGHTNESS = 0.18;
constexpr double CROSSING_BRIGHTNESS = 0.26;
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

void draw_graticule(const View& view, const CellGrid& grid, vtscreen::Picture& picture, LayerBlend& blend) {
    const int cols = grid.cols(), rows = grid.rows();

    // degrees of longitude in one column at the centre. From the ground distance, not from two points on the screen,
    // so that it does not change when the map is turned.
    const double lon_per_col = view.km_per_col() / (KM_PER_DEGREE * std::cos(view.centre().lat * PI / 180.0));
    const double step = nice_step(MIN_CELLS_BETWEEN_LINES * lon_per_col);
    const int decimals = std::max(0, -(int)std::floor(std::log10(step) + 1e-9));

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const GeoBox cell = grid.box(c, r);
            const bool lat_line = line_between(cell.south, cell.north, step);
            const bool lon_line = line_between(cell.west, cell.east, step);
            if (!lat_line && !lon_line) continue;

            blend.add(r, c, Layer::Grid, lat_line && lon_line ? CROSSING_BRIGHTNESS : LINE_BRIGHTNESS, 90, 135, 175);

            // labels: latitudes down the left edge, longitudes along the bottom
            if (lat_line && c == 0)
                picture.text(r, 0, degrees_text(std::floor(cell.north / step) * step, 'N', 'S', decimals),
                             110, 160, 205);
            if (lon_line && r == rows - 1)
                picture.text(r, c + 1, degrees_text(std::floor(cell.east / step) * step, 'E', 'W', decimals),
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

// Retired: the readout in the top left is now the cards (cards.h).
#if 0
void draw_readout(const View& view, const Settings& settings, const std::string& notes,
                  const std::string& radar_status, vtscreen::Picture& picture) {
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
    picture.text(row++, 1, radar_status, 150, 200, 150);
    picture.text(row++, 1, "+ - zoom  arrows move  / go to  r reset  g grid  n names  k key  q quit", 120, 120, 130);

    size_t start = 0;
    while (start < notes.size()) {                           // one amber line per note
        size_t end = notes.find('\n', start);
        if (end == std::string::npos) end = notes.size();
        picture.text(row++, 1, notes.substr(start, end - start), 255, 190, 80);
        start = end + 1;
    }
}

#endif

}  // namespace vtw
