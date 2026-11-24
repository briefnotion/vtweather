#include "ground_map.h"

#include <algorithm>
#include <cmath>

namespace vtw {

namespace {

bool contains(const GeoBox& outer, const GeoBox& inner) {
    return outer.south <= inner.south && outer.north >= inner.north && outer.west <= inner.west &&
           outer.east >= inner.east;
}

bool overlaps(const GeoBox& a, const GeoBox& b) {
    return a.south <= b.north && a.north >= b.south && a.west <= b.east && a.east >= b.west;
}

const MapLayer* find_layer(const BaseMap& map, LayerKind kind, int detail) {
    for (const MapLayer& layer : map.layers)
        if (layer.kind == kind && layer.detail == detail) return &layer;
    return nullptr;
}

// Fill the cells inside a polygon (even-odd over all its rings) with a value.
class Filler {
public:
    Filler(const View& view, std::vector<uint8_t>& cells) : view_(view), cells_(cells), crossings_(view.rows()) {}

    void fill(const MapPolygon& polygon, uint8_t value) {
        const int cols = view_.cols(), rows = view_.rows();
        int first_row = rows, last_row = -1;
        for (const std::vector<int32_t>& ring : polygon.rings) {
            const size_t n = ring.size() / 2;
            if (n < 3) continue;
            double px = 0, py = 0;
            for (size_t i = 0; i <= n; i++) {                    // i == n closes the ring
                const size_t k = i % n;
                double x, y;
                view_.to_cell(GeoPoint{ring[2 * k + 1] / MAP_UNITS_PER_DEGREE, ring[2 * k] / MAP_UNITS_PER_DEGREE}, x, y);
                if (i > 0) add_edge(px, py, x, y, first_row, last_row);
                px = x; py = y;
            }
        }
        for (int row = std::max(first_row, 0); row <= std::min(last_row, rows - 1); row++) {
            std::vector<double>& xs = crossings_[row];
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                const int from = std::max(0, (int)std::ceil(xs[i] - 0.5));
                const int to = std::min(cols - 1, (int)std::ceil(xs[i + 1] - 0.5) - 1);
                for (int col = from; col <= to; col++) cells_[(size_t)row * cols + col] = value;
            }
            xs.clear();
        }
    }

private:
    // the scan lines (through the middle of each row) that this edge crosses, and where
    void add_edge(double x0, double y0, double x1, double y1, int& first_row, int& last_row) {
        if (y0 == y1) return;
        const double low = std::min(y0, y1), high = std::max(y0, y1);
        const int rows = view_.rows();
        const int from = std::max(0, (int)std::ceil(low - 0.5));
        const int to = std::min(rows - 1, (int)std::ceil(high - 0.5) - 1);
        for (int row = from; row <= to; row++)
            crossings_[row].push_back(x0 + (row + 0.5 - y0) * (x1 - x0) / (y1 - y0));
        if (from <= to) { first_row = std::min(first_row, from); last_row = std::max(last_row, to); }
    }

    const View& view_;
    std::vector<uint8_t>& cells_;
    std::vector<std::vector<double>> crossings_;     // per row: where the outline crosses it
};

}  // namespace

int choose_detail(const BaseMap& map, const View& view) {
    int wanted = view.zoom_km() >= MEDIUM_DETAIL_BELOW_KM ? 0 : (view.zoom_km() >= FINE_DETAIL_BELOW_KM ? 1 : 2);
    const GeoBox screen = view.box(0.05);
    for (int detail = wanted; detail >= 0; detail--) {
        const MapLayer* land = find_layer(map, LayerKind::Land, detail);
        if (land && contains(land->coverage, screen)) return detail;
    }
    for (int detail = 0; detail < DETAIL_LEVELS; detail++)      // nothing covers it: the coarsest there is
        if (find_layer(map, LayerKind::Land, detail)) return detail;
    return -1;
}

void GroundMap::update(const View& view) {
    if (have_ && cols_ == view.cols() && rows_ == view.rows() && zoom_ == view.zoom_km() &&
        aspect_ == view.cell_aspect() && heading_ == view.heading() && centre_.lat == view.centre().lat &&
        centre_.lon == view.centre().lon)
        return;
    have_ = true;
    cols_ = view.cols(); rows_ = view.rows();
    zoom_ = view.zoom_km(); aspect_ = view.cell_aspect(); heading_ = view.heading(); centre_ = view.centre();

    cells_.assign((size_t)cols_ * rows_, (uint8_t)Ground::Water);
    detail_ = choose_detail(map_, view);
    if (detail_ < 0) return;

    const GeoBox screen = view.box(0.05);
    std::vector<uint8_t> land((size_t)cols_ * rows_, 0);
    Filler filler(view, land);
    if (const MapLayer* layer = find_layer(map_, LayerKind::Land, detail_))
        for (const MapPolygon& polygon : layer->polygons)
            if (overlaps(polygon.box, screen)) filler.fill(polygon, 1);
    if (const MapLayer* layer = find_layer(map_, LayerKind::Lake, detail_))
        for (const MapPolygon& polygon : layer->polygons)
            if (overlaps(polygon.box, screen)) filler.fill(polygon, 0);

    // land, and land with water next to it: the coast
    for (int row = 0; row < rows_; row++)
        for (int col = 0; col < cols_; col++) {
            if (!land[(size_t)row * cols_ + col]) continue;
            const bool water_near = (col > 0 && !land[(size_t)row * cols_ + col - 1]) ||
                                    (col + 1 < cols_ && !land[(size_t)row * cols_ + col + 1]) ||
                                    (row > 0 && !land[(size_t)(row - 1) * cols_ + col]) ||
                                    (row + 1 < rows_ && !land[(size_t)(row + 1) * cols_ + col]);
            cells_[(size_t)row * cols_ + col] = (uint8_t)(water_near ? Ground::Coast : Ground::Land);
        }
}

}  // namespace vtw
