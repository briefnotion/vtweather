#include "rain_contours.h"

#include <algorithm>
#include <cmath>

namespace vtw {

namespace {

// ---- the lattice ----

float bilinear(const RadarFrame& frame, double lat, double lon, double pixel_w, double pixel_h) {
    const double fx = (lon - frame.box.west) / pixel_w - 0.5, fy = (frame.box.north - lat) / pixel_h - 0.5;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const double tx = fx - x0, ty = fy - y0;
    auto pixel = [&](int x, int y) -> double {
        return (x >= 0 && y >= 0 && x < frame.width && y < frame.height) ? frame.at(x, y) : 0.0;
    };
    const double top = pixel(x0, y0) * (1 - tx) + pixel(x0 + 1, y0) * tx;
    const double bottom = pixel(x0, y0 + 1) * (1 - tx) + pixel(x0 + 1, y0 + 1) * tx;
    return (float)(top * (1 - ty) + bottom * ty);
}

// ---- marching squares ----

// The edges of a lattice cell, as numbers: a crossing point is named by the edge it is on, so two cells that share an edge
// agree on it exactly and the lines join up without comparing positions.
struct Edges {
    int nx, ny;
    int horizontal(int i, int j) const { return j * (nx - 1) + i; }                          // between node (i, j) and (i + 1, j)
    int vertical(int i, int j) const { return (nx - 1) * ny + j * nx + i; }                  // between node (i, j) and (i, j + 1)
    int count() const { return (nx - 1) * ny + nx * (ny - 1); }
};

// Where along an edge the level is (0 at the first node, 1 at the second). Only asked for an edge with one node at or above the level
// and the other below it, so the two differ and the answer is between 0 and 1.
double crossing(double v1, double v2, double level) { return (level - v1) / (v2 - v1); }

// The pieces of one cell's contour as pairs of edge sides: 0 top, 1 right, 2 bottom, 3 left.
void cell_segments(int mask, bool centre_inside, int out[2][2], int& count) {
    count = 0;
    auto add = [&](int a, int b) { out[count][0] = a; out[count][1] = b; count++; };
    switch (mask) {
        case 1: add(3, 0); break;
        case 2: add(0, 1); break;
        case 3: add(3, 1); break;
        case 4: add(1, 2); break;
        case 5: if (centre_inside) { add(0, 1); add(3, 2); } else { add(3, 0); add(1, 2); } break;
        case 6: add(0, 2); break;
        case 7: add(3, 2); break;
        case 8: add(3, 2); break;
        case 9: add(0, 2); break;
        case 10: if (centre_inside) { add(3, 0); add(1, 2); } else { add(0, 1); add(3, 2); } break;
        case 11: add(1, 2); break;
        case 12: add(3, 1); break;
        case 13: add(0, 1); break;
        case 14: add(3, 0); break;
        default: break;
    }
}

// ---- tidying ----

// Douglas-Peucker on points whose rows are scaled to the same size as the columns.
void simplify_range(const std::vector<CellPoint>& p, double aspect, double tolerance, size_t first, size_t last, std::vector<char>& keep) {
    std::vector<std::pair<size_t, size_t>> todo{{first, last}};
    while (!todo.empty()) {
        const auto range = todo.back();
        todo.pop_back();
        const double ax = p[range.first].col, ay = p[range.first].row * aspect, bx = p[range.second].col, by = p[range.second].row * aspect;
        const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
        double far = -1;
        size_t at = range.first;
        for (size_t i = range.first + 1; i < range.second; i++) {
            const double qx = p[i].col, qy = p[i].row * aspect;
            double t = len2 > 0 ? ((qx - ax) * dx + (qy - ay) * dy) / len2 : 0;
            t = std::max(0.0, std::min(1.0, t));
            const double d = std::hypot(qx - (ax + t * dx), qy - (ay + t * dy));
            if (d > far) { far = d; at = i; }
        }
        if (far > tolerance) {
            keep[at] = 1;
            todo.push_back({range.first, at});
            todo.push_back({at, range.second});
        }
    }
}

double loop_area(const std::vector<CellPoint>& p, double aspect) {
    double twice = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const CellPoint& a = p[i];
        const CellPoint& b = p[(i + 1) % p.size()];
        twice += a.col * (b.row * aspect) - b.col * (a.row * aspect);
    }
    return std::fabs(twice) / 2;
}

double line_length(const std::vector<CellPoint>& p, double aspect) {
    double n = 0;
    for (size_t i = 1; i < p.size(); i++) n += std::hypot(p[i].col - p[i - 1].col, (p[i].row - p[i - 1].row) * aspect);
    return n;
}

}  // namespace

int rain_band_of(double dbz) {
    int band = -1;
    for (int k = 0; k < RAIN_BANDS; k++)
        if (dbz >= RAIN_BAND_MIN_DBZ[k]) band = k;
    return band;
}

RainField sample_rain_field(const RadarFrame& frame, const View& view, bool peaks) {
    RainField field;
    if (frame.width <= 0 || frame.height <= 0 || view.cols() <= 0 || view.rows() <= 0) return field;
    const int cols = view.cols(), rows = view.rows();
    field.nx = (int)std::ceil(cols / FIELD_STEP_COLS) + 1;
    field.ny = (int)std::ceil(rows / FIELD_STEP_ROWS) + 1;
    field.step_col = (double)cols / (field.nx - 1);
    field.step_row = (double)rows / (field.ny - 1);
    field.value.assign((size_t)field.nx * field.ny, 0.0f);

    const double pixel_w = (frame.box.east - frame.box.west) / frame.width;
    const double pixel_h = (frame.box.north - frame.box.south) / frame.height;
    // the earth at the corners of every node's footprint (half a step beyond it each way), worked out once each
    const int cx = field.nx + 1, cy = field.ny + 1;
    std::vector<GeoPoint> corner((size_t)cx * cy);
    for (int j = 0; j < cy; j++)
        for (int i = 0; i < cx; i++) corner[(size_t)j * cx + i] = view.at((i - 0.5) * field.step_col, (j - 0.5) * field.step_row);
    for (int j = 0; j < field.ny; j++)
        for (int i = 0; i < field.nx; i++) {
            const GeoPoint& a = corner[(size_t)j * cx + i];
            const GeoPoint& b = corner[(size_t)j * cx + i + 1];
            const GeoPoint& c = corner[(size_t)(j + 1) * cx + i];
            const GeoPoint& d = corner[(size_t)(j + 1) * cx + i + 1];
            GeoBox area;
            area.south = std::min({a.lat, b.lat, c.lat, d.lat}); area.north = std::max({a.lat, b.lat, c.lat, d.lat});
            area.west = std::min({a.lon, b.lon, c.lon, d.lon}); area.east = std::max({a.lon, b.lon, c.lon, d.lon});
            float v;
            if (peaks || area.east - area.west > 1.5 * pixel_w || area.north - area.south > 1.5 * pixel_h) {
                v = frame.strongest_in(area);                                         // many pixels under one node: the strongest
            } else {                                                                  // finer than the pixels: smooth between them
                v = bilinear(frame, (area.south + area.north) / 2, (area.west + area.east) / 2, pixel_w, pixel_h);
            }
            field.value[(size_t)j * field.nx + i] = v;
        }
    return field;
}

void smooth_field(RainField& field) {
    std::vector<float> out(field.value.size());
    for (int j = 0; j < field.ny; j++)
        for (int i = 0; i < field.nx; i++) {
            double sum = 0;
            for (int dj = -1; dj <= 1; dj++)
                for (int di = -1; di <= 1; di++) {
                    const int ii = std::max(0, std::min(field.nx - 1, i + di)), jj = std::max(0, std::min(field.ny - 1, j + dj));
                    sum += field.at(ii, jj);
                }
            out[(size_t)j * field.nx + i] = (float)(sum / 9.0);
        }
    field.value = std::move(out);
}

std::vector<ContourLine> trace_contours(const RainField& field, double level) {
    std::vector<ContourLine> lines;
    if (field.nx < 2 || field.ny < 2) return lines;
    const Edges edges{field.nx, field.ny};
    const int n = edges.count();
    std::vector<int> neighbour_a((size_t)n, -1), neighbour_b((size_t)n, -1);       // the (up to two) crossings each is joined to
    std::vector<CellPoint> where((size_t)n);
    std::vector<char> have((size_t)n, 0);

    auto point_on = [&](int side, int i, int j, int& id) -> CellPoint {
        // the crossing on one side of cell (i, j): its edge's number, and where along the edge the level is
        int i1, j1, i2, j2;
        switch (side) {
            case 0: id = edges.horizontal(i, j); i1 = i; j1 = j; i2 = i + 1; j2 = j; break;
            case 1: id = edges.vertical(i + 1, j); i1 = i + 1; j1 = j; i2 = i + 1; j2 = j + 1; break;
            case 2: id = edges.horizontal(i, j + 1); i1 = i; j1 = j + 1; i2 = i + 1; j2 = j + 1; break;
            default: id = edges.vertical(i, j); i1 = i; j1 = j; i2 = i; j2 = j + 1; break;
        }
        const double t = crossing(field.at(i1, j1), field.at(i2, j2), level);
        return {(i1 + t * (i2 - i1)) * field.step_col, (j1 + t * (j2 - j1)) * field.step_row};
    };
    auto join = [&](int a, int b) {
        auto attach = [&](int from, int to) {
            if (neighbour_a[(size_t)from] < 0) neighbour_a[(size_t)from] = to;
            else neighbour_b[(size_t)from] = to;
        };
        attach(a, b);
        attach(b, a);
    };

    for (int j = 0; j + 1 < field.ny; j++)
        for (int i = 0; i + 1 < field.nx; i++) {
            const double a = field.at(i, j), b = field.at(i + 1, j), c = field.at(i + 1, j + 1), d = field.at(i, j + 1);
            const int mask = (a >= level ? 1 : 0) | (b >= level ? 2 : 0) | (c >= level ? 4 : 0) | (d >= level ? 8 : 0);
            if (mask == 0 || mask == 15) continue;
            int segments[2][2], count;
            cell_segments(mask, (a + b + c + d) / 4 >= level, segments, count);
            for (int k = 0; k < count; k++) {
                int id0, id1;
                const CellPoint p0 = point_on(segments[k][0], i, j, id0), p1 = point_on(segments[k][1], i, j, id1);
                if (!have[(size_t)id0]) { have[(size_t)id0] = 1; where[(size_t)id0] = p0; }
                if (!have[(size_t)id1]) { have[(size_t)id1] = 1; where[(size_t)id1] = p1; }
                join(id0, id1);
            }
        }

    // follow the lines: first the ones that run off the edge of the lattice (a crossing joined to one other), then the loops
    std::vector<char> done((size_t)n, 0);
    auto follow = [&](int start, bool closed) {
        ContourLine line;
        line.closed = closed;
        int previous = -1, current = start;
        while (current >= 0 && !done[(size_t)current]) {
            done[(size_t)current] = 1;
            line.points.push_back(where[(size_t)current]);
            int next = neighbour_a[(size_t)current];
            if (next == previous || next < 0 || done[(size_t)next]) next = neighbour_b[(size_t)current];
            if (next >= 0 && done[(size_t)next]) next = -1;
            previous = current;
            current = next;
        }
        if (line.points.size() >= 2) lines.push_back(std::move(line));
    };
    for (int id = 0; id < n; id++)
        if (have[(size_t)id] && !done[(size_t)id] && neighbour_a[(size_t)id] >= 0 && neighbour_b[(size_t)id] < 0) follow(id, false);
    for (int id = 0; id < n; id++)
        if (have[(size_t)id] && !done[(size_t)id] && neighbour_a[(size_t)id] >= 0) follow(id, true);
    return lines;
}

ContourLine round_contour(const ContourLine& line) {
    ContourLine out;
    out.closed = line.closed;
    const size_t n = line.points.size();
    if (n < 3) { out.points = line.points; return out; }
    auto lerp = [](const CellPoint& a, const CellPoint& b, double t) { return CellPoint{a.col + t * (b.col - a.col), a.row + t * (b.row - a.row)}; };
    if (!line.closed) out.points.push_back(line.points.front());
    const size_t segments = line.closed ? n : n - 1;
    for (size_t i = 0; i < segments; i++) {
        const CellPoint& a = line.points[i];
        const CellPoint& b = line.points[(i + 1) % n];
        const bool first = !line.closed && i == 0, last = !line.closed && i + 1 == segments;
        if (!first) out.points.push_back(lerp(a, b, 0.25));                    // (an open line keeps its end points as they are)
        if (!last) out.points.push_back(lerp(a, b, 0.75));
    }
    if (!line.closed) out.points.push_back(line.points.back());
    return out;
}

std::vector<ContourLine> tidy_contours(const std::vector<ContourLine>& lines, double aspect, double min_loop_area, double min_length,
                                       double marker_radius) {
    std::vector<ContourLine> out;
    for (const ContourLine& line : lines) {
        std::vector<CellPoint> points = line.points;
        if (line.closed && points.size() >= 3) points.push_back(points.front());          // simplify it as a line from the start back to it
        std::vector<char> keep(points.size(), 0);
        keep.front() = keep.back() = 1;
        simplify_range(points, aspect, SIMPLIFY_TOLERANCE, 0, points.size() - 1, keep);
        ContourLine tidy;
        tidy.closed = line.closed;
        for (size_t i = 0; i < points.size(); i++)
            if (keep[i]) tidy.points.push_back(points[i]);
        if (tidy.closed && !tidy.points.empty()) tidy.points.pop_back();                   // the closing point is implied
        if (tidy.closed) {
            if (tidy.points.size() < 3 || loop_area(tidy.points, aspect) < min_loop_area) {
                if (marker_radius > 0 && !line.points.empty()) {                 // too small to follow: a small circle round its middle
                    CellPoint middle{0, 0};
                    for (const CellPoint& p : line.points) { middle.col += p.col / line.points.size(); middle.row += p.row / line.points.size(); }
                    tidy.points.clear();
                    for (int k = 0; k < 8; k++) {
                        const double a = k * 3.14159265358979 / 4;
                        tidy.points.push_back({middle.col + marker_radius * std::cos(a), middle.row + marker_radius / aspect * std::sin(a)});
                    }
                    out.push_back(std::move(tidy));
                }
                continue;
            }
        } else {
            if (tidy.points.size() < 2 || line_length(tidy.points, aspect) < min_length) continue;
        }
        out.push_back(std::move(tidy));
    }
    return out;
}

RainContours make_rain_contours(const RadarFrame& frame, const View& view) {
    RainContours result;
    RainField smooth = sample_rain_field(frame, view, false);
    if (smooth.nx == 0) return result;
    const RainField raw = sample_rain_field(frame, view, true);
    smooth_field(smooth);
    for (int k = 0; k < RAIN_BANDS; k++) {
        const RainField& field = k == 0 ? smooth : raw;                        // the stronger bands keep their small cores
        std::vector<ContourLine> lines = trace_contours(field, RAIN_BAND_MIN_DBZ[k]);
        for (int pass = 0; pass < (k == 0 ? 1 : 2); pass++)                    // (the field of the stronger bands is blockier: round it more)
            for (ContourLine& line : lines) line = round_contour(line);
        result.band[k] = tidy_contours(lines, view.cell_aspect(), MIN_LOOP_AREA_BY_BAND[k], MIN_LINE_LENGTH_BY_BAND[k], MARKER_RADIUS_BY_BAND[k]);
    }
    return result;
}

}  // namespace vtw
