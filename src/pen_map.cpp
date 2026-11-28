#include "pen_map.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "ground_map.h"

namespace vtw {

namespace {

constexpr double TINY = 1e-12;

// The part of the segment a to b inside the box 0..w by 0..h, as the range t0..t1 of the way along it (Liang-Barsky).
bool clip_to_box(CellPoint a, CellPoint b, double w, double h, double& t0, double& t1) {
    t0 = 0; t1 = 1;
    const double dx = b.col - a.col, dy = b.row - a.row;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.col, w - a.col, a.row, h - a.row};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0) { if (t > t1) return false; t0 = std::max(t0, t); }
            else { if (t < t0) return false; t1 = std::min(t1, t); }
        }
    }
    return true;                                                            // (each bound was checked against the other as it was found)
}

// The stretches of the segment a to b (as ranges of the way along it) that are inside the cells and in no blocked cell.
// The cells the segment passes through are walked one by one (a grid walk), so nothing is missed between samples.
void visible_ranges(CellPoint a, CellPoint b, int cols, int rows, const Blocked& blocked, std::vector<std::pair<double, double>>& out) {
    out.clear();
    double t0, t1;
    if (!clip_to_box(a, b, cols, rows, t0, t1)) return;
    const double dx = b.col - a.col, dy = b.row - a.row;
    const double ax = a.col + t0 * dx, ay = a.row + t0 * dy, bx = a.col + t1 * dx, by = a.row + t1 * dy;
    const double sx = bx - ax, sy = by - ay;
    if (std::fabs(sx) < TINY && std::fabs(sy) < TINY) return;               // no length: nothing to draw
    int ix = std::max(0, std::min(cols - 1, (int)std::floor(ax)));
    int iy = std::max(0, std::min(rows - 1, (int)std::floor(ay)));         // (a start on a cell edge, heading back, begins in the cell behind it: its first step is of no length and is skipped)
    const double inf = std::numeric_limits<double>::infinity();
    const int step_x = sx > 0 ? 1 : (sx < 0 ? -1 : 0), step_y = sy > 0 ? 1 : (sy < 0 ? -1 : 0);
    double tmax_x = sx > 0 ? (ix + 1 - ax) / sx : (sx < 0 ? (ix - ax) / sx : inf);
    double tmax_y = sy > 0 ? (iy + 1 - ay) / sy : (sy < 0 ? (iy - ay) / sy : inf);
    const double tdelta_x = sx != 0 ? std::fabs(1.0 / sx) : inf, tdelta_y = sy != 0 ? std::fabs(1.0 / sy) : inf;
    double u = 0;
    for (int guard = 0; guard < cols + rows + 8; guard++) {
        const double un = std::min(1.0, std::min(tmax_x, tmax_y));
        if (un > u && !blocked[(size_t)iy * cols + ix]) {
            const double from = t0 + u * (t1 - t0), to = t0 + un * (t1 - t0);
            if (!out.empty() && out.back().second >= from - TINY) out.back().second = to;      // goes on from the last cell
            else out.emplace_back(from, to);
        }
        if (un >= 1.0) break;
        u = un;
        const bool advance_x = tmax_x <= un, advance_y = tmax_y <= un;
        if (advance_x) { ix += step_x; tmax_x += tdelta_x; }
        if (advance_y) { iy += step_y; tmax_y += tdelta_y; }
        if (ix < 0 || ix >= cols || iy < 0 || iy >= rows) break;
    }
}

bool finite(CellPoint p) { return std::isfinite(p.col) && std::isfinite(p.row); }

bool overlaps(const GeoBox& a, const GeoBox& b) {
    return a.south <= b.north && a.north >= b.south && a.west <= b.east && a.east >= b.west;
}

CellPoint project(const View& view, int32_t lon_units, int32_t lat_units) {
    CellPoint p;
    view.to_cell(GeoPoint{lat_units / MAP_UNITS_PER_DEGREE, lon_units / MAP_UNITS_PER_DEGREE}, p.col, p.row);
    return p;
}

// A segment of a polygon's outline that only lies along the edge of the area the layer covers is not a coast: it is where
// the file was cut (the world's own edges, the date line and the poles, are the edges of the coarsest layer). Within 2 units
// (2e-5 degrees).
bool along_cut_edge(int32_t lon0, int32_t lat0, int32_t lon1, int32_t lat1, const GeoBox& coverage) {
    const double units = MAP_UNITS_PER_DEGREE;
    auto near = [](double a, double b) { return std::fabs(a - b) <= 2.0; };
    const double w = coverage.west * units, e = coverage.east * units, s = coverage.south * units, n = coverage.north * units;
    if (near(lon0, w) && near(lon1, w)) return true;
    if (near(lon0, e) && near(lon1, e)) return true;
    if (near(lat0, s) && near(lat1, s)) return true;
    if (near(lat0, n) && near(lat1, n)) return true;
    return false;
}

// Collects the drawn lines of one kind, cut to the map and kept clear of the blocked cells.
class LineAdder {
public:
    LineAdder(vtpen::Drawing& drawing, int cols, int rows, int cell_w, int cell_h, const Blocked& blocked, bool white)
        : drawing_(drawing), cols_(cols), rows_(rows), cw_(cell_w), ch_(cell_h), blocked_(blocked), white_(white) {}

    void style(const PenStyle& s) {
        if (white_) drawing_.set_colour({255, 255, 255});
        else drawing_.set_colour({s.r, s.g, s.b});
        drawing_.set_thickness(s.thickness);
    }

    void add(const std::vector<CellPoint>& line) {
        pieces_.clear();
        clip_to_cells(line, cols_, rows_, blocked_, pieces_);
        for (const std::vector<CellPoint>& piece : pieces_) {
            std::vector<vtpen::Point> points;
            points.reserve(piece.size());
            for (const CellPoint& p : piece) points.push_back({p.col * cw_, p.row * ch_});
            drawing_.polyline(points);
        }
    }

private:
    vtpen::Drawing& drawing_;
    int cols_, rows_, cw_, ch_;
    const Blocked& blocked_;
    bool white_;
    std::vector<std::vector<CellPoint>> pieces_;
};

void add_lines(const MapLayer& layer, const View& view, const GeoBox& screen, LineAdder& adder) {
    for (const MapLine& line : layer.lines) {
        if (!overlaps(line.box, screen)) continue;
        std::vector<CellPoint> cells;
        const size_t n = line.points.size() / 2;
        cells.reserve(n);
        for (size_t i = 0; i < n; i++) cells.push_back(project(view, line.points[2 * i], line.points[2 * i + 1]));
        adder.add(cells);
    }
}

// The outlines of polygons (their rings), except where the file was cut.
void add_outlines(const MapLayer& layer, const View& view, const GeoBox& screen, LineAdder& adder) {
    for (const MapPolygon& polygon : layer.polygons) {
        if (!overlaps(polygon.box, screen)) continue;
        for (const std::vector<int32_t>& ring : polygon.rings) {
            const size_t n = ring.size() / 2;
            if (n < 3) continue;
            std::vector<CellPoint> chain;
            for (size_t i = 1; i <= n; i++) {                                     // the segment from point i-1 to point i (the last one closes the ring)
                const size_t a = i - 1, b = i % n;
                if (along_cut_edge(ring[2 * a], ring[2 * a + 1], ring[2 * b], ring[2 * b + 1], layer.coverage)) {
                    if (chain.size() > 1) adder.add(chain);
                    chain.clear();
                    continue;
                }
                if (chain.empty()) chain.push_back(project(view, ring[2 * a], ring[2 * a + 1]));
                chain.push_back(project(view, ring[2 * b], ring[2 * b + 1]));
            }
            if (chain.size() > 1) adder.add(chain);
        }
    }
}

}  // namespace

Blocked blocked_cells(const vtscreen::Picture& picture, int cols, int rows) {
    Blocked blocked((size_t)std::max(0, cols) * (size_t)std::max(0, rows), 0);
    for (int row = 0; row < rows && row < picture.rows; row++)
        for (int col = 0; col < cols && col < picture.cols; col++)
            if (picture.cell[(size_t)row * picture.cols + col].text) blocked[(size_t)row * cols + col] = 1;
    return blocked;
}

void clip_to_cells(const std::vector<CellPoint>& line, int cols, int rows, const Blocked& blocked,
                   std::vector<std::vector<CellPoint>>& pieces) {
    if (cols <= 0 || rows <= 0 || blocked.size() != (size_t)cols * (size_t)rows) return;
    std::vector<CellPoint> current;
    auto flush = [&]() {
        if (current.size() > 1) pieces.push_back(current);
        current.clear();
    };
    std::vector<std::pair<double, double>> ranges;
    for (size_t i = 1; i < line.size(); i++) {
        const CellPoint a = line[i - 1], b = line[i];
        if (!finite(a) || !finite(b)) { flush(); continue; }
        visible_ranges(a, b, cols, rows, blocked, ranges);
        if (ranges.empty()) { flush(); continue; }
        for (size_t k = 0; k < ranges.size(); k++) {
            const CellPoint from{a.col + ranges[k].first * (b.col - a.col), a.row + ranges[k].first * (b.row - a.row)};
            const CellPoint to{a.col + ranges[k].second * (b.col - a.col), a.row + ranges[k].second * (b.row - a.row)};
            const bool continues = !current.empty() && std::fabs(current.back().col - from.col) < 1e-9 && std::fabs(current.back().row - from.row) < 1e-9;
            if (!continues) { flush(); current.push_back(from); }
            current.push_back(to);
        }
    }
    flush();
}

PenStyle pen_style(LayerKind kind) {
    switch (kind) {
        case LayerKind::River: return {70, 140, 205, 1};
        case LayerKind::Road: return {165, 135, 80, 1};
        case LayerKind::Highway: return {230, 180, 85, 1};
        case LayerKind::State: return {150, 120, 175, 1};
        case LayerKind::Country: return {215, 120, 150, 1};
        default: return pen_style_coast();
    }
}

PenStyle pen_style_coast() { return {190, 170, 120, 1}; }

std::shared_ptr<const vtpen::Drawing> make_pen_map(const BaseMap& map, const View& view, int cell_w, int cell_h, const Blocked& blocked, bool white) {
    if (cell_w <= 0 || cell_h <= 0 || view.cols() <= 0 || view.rows() <= 0) return nullptr;
    auto drawing = std::make_shared<vtpen::Drawing>(view.cols() * cell_w, view.rows() * cell_h);
    const int detail = choose_detail(map, view);
    if (detail < 0) return drawing;
    const GeoBox screen = view.box(0.05);
    Blocked all_clear;                                           // used if the cells given are not the size of the view
    const Blocked* mask = &blocked;
    if (blocked.size() != (size_t)view.cols() * (size_t)view.rows()) {
        all_clear.assign((size_t)view.cols() * (size_t)view.rows(), 0);
        mask = &all_clear;
    }
    LineAdder adder(*drawing, view.cols(), view.rows(), cell_w, cell_h, *mask, white);

    // lowest first, so what is drawn later is on top: rivers, roads, highways, the coast, state borders, country borders
    if (const MapLayer* layer = find_layer(map, LayerKind::River, detail)) { adder.style(pen_style(LayerKind::River)); add_lines(*layer, view, screen, adder); }
    if (view.zoom_km() < ROADS_BELOW_KM)
        if (const MapLayer* layer = find_layer(map, LayerKind::Road, DETAIL_LEVELS - 1)) { adder.style(pen_style(LayerKind::Road)); add_lines(*layer, view, screen, adder); }
    if (view.zoom_km() < HIGHWAYS_BELOW_KM)
        if (const MapLayer* layer = find_layer(map, LayerKind::Highway, DETAIL_LEVELS - 1)) { adder.style(pen_style(LayerKind::Highway)); add_lines(*layer, view, screen, adder); }
    adder.style(pen_style_coast());
    if (const MapLayer* layer = find_layer(map, LayerKind::Land, detail)) add_outlines(*layer, view, screen, adder);
    if (const MapLayer* layer = find_layer(map, LayerKind::Lake, detail)) add_outlines(*layer, view, screen, adder);
    if (const MapLayer* layer = find_layer(map, LayerKind::State, detail)) { adder.style(pen_style(LayerKind::State)); add_lines(*layer, view, screen, adder); }
    if (const MapLayer* layer = find_layer(map, LayerKind::Country, detail)) { adder.style(pen_style(LayerKind::Country)); add_lines(*layer, view, screen, adder); }
    return drawing;
}

void PenMap::update(const View& view, vtscreen::Picture& picture, int cell_w, int cell_h, double now, bool on, bool white) {
    if (!on || cell_w <= 0 || cell_h <= 0) {
        drawing_.reset();
        have_ = false;
        return;
    }
    const bool same_size = have_ && cols_ == view.cols() && rows_ == view.rows() && cell_w_ == cell_w && cell_h_ == cell_h && white_ == white;
    Blocked blocked = blocked_cells(picture, view.cols(), view.rows());
    const bool same = same_size && centre_.lat == view.centre().lat && centre_.lon == view.centre().lon && zoom_ == view.zoom_km() &&
                      aspect_ == view.cell_aspect() && heading_ == view.heading() && blocked == blocked_;
    if (!same && (!same_size || now - made_at_ >= PEN_MAP_MIN_SECONDS)) {
        drawing_ = make_pen_map(map_, view, cell_w, cell_h, blocked, white);
        cols_ = view.cols(); rows_ = view.rows(); cell_w_ = cell_w; cell_h_ = cell_h; white_ = white;
        centre_ = view.centre(); zoom_ = view.zoom_km(); aspect_ = view.cell_aspect(); heading_ = view.heading();
        blocked_ = std::move(blocked);
        made_at_ = now;
        made_++;
        have_ = true;
    }
    if (drawing_) picture.place_drawing(drawing_, 0, 0, view.rows(), view.cols());
}

}  // namespace vtw
