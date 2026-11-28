#include "rain_pen.h"

#include <algorithm>
#include <cmath>

namespace vtw {

DashSizes dash_sizes(int cell_w) {
    const double s = std::max(0.5, std::min(3.0, cell_w / 13.0));
    return {2.0 * s, 6.0 * s, 12.0 * s, 6.0 * s, std::max(2.2, 2.4 * s)};
}

std::vector<std::vector<CellPoint>> dash_line(const std::vector<CellPoint>& line, double on, double off) {
    std::vector<std::vector<CellPoint>> pieces;
    if (line.size() < 2 || !(on > 0) || !(off >= 0)) return pieces;
    if (off == 0) { pieces.push_back(line); return pieces; }                // no gaps: the line itself
    std::vector<CellPoint> current;
    bool drawing = true;
    double left = on;                                         // what is left of this dash, or of this gap
    current.push_back(line[0]);
    for (size_t i = 1; i < line.size(); i++) {
        CellPoint a = line[i - 1];
        const CellPoint b = line[i];
        double length = std::hypot(b.col - a.col, b.row - a.row);
        while (length > 0) {
            if (length <= left) {                              // the segment ends inside this dash or gap
                if (drawing) current.push_back(b);
                left -= length;
                length = 0;
                break;
            }
            const double t = left / length;
            const CellPoint cut{a.col + t * (b.col - a.col), a.row + t * (b.row - a.row)};
            if (drawing) {
                current.push_back(cut);
                if (current.size() >= 2) pieces.push_back(current);
                current.clear();
            } else {
                current.push_back(cut);
            }
            length -= left;
            a = cut;
            drawing = !drawing;
            left = drawing ? on : off;
        }
    }
    if (drawing && current.size() >= 2) pieces.push_back(current);
    return pieces;
}

std::vector<CellPoint> offset_line(const std::vector<CellPoint>& line, double distance) {
    std::vector<CellPoint> out;
    const size_t n = line.size();
    if (n < 2) return line;
    auto normal = [&](size_t i) {                              // the left-hand normal of segment i to i+1
        const double dx = line[i + 1].col - line[i].col, dy = line[i + 1].row - line[i].row, len = std::hypot(dx, dy);
        return len > 0 ? CellPoint{dy / len, -dx / len} : CellPoint{0, 0};
    };
    for (size_t i = 0; i < n; i++) {
        CellPoint m;
        if (i == 0) m = normal(0);
        else if (i + 1 == n) m = normal(n - 2);
        else {
            const CellPoint a = normal(i - 1), b = normal(i);
            m = {a.col + b.col, a.row + b.row};
            const double len = std::hypot(m.col, m.row);
            if (len > 1e-9) {
                m = {m.col / len, m.row / len};
                const double dot = m.col * a.col + m.row * a.row;       // how far round the bend: the mitre is 1 / dot out
                const double scale = 1.0 / std::max(0.5, dot);
                m = {m.col * scale, m.row * scale};
            } else {
                m = a;                                          // doubled back on itself: no sensible mitre
            }
        }
        out.push_back({line[i].col + m.col * distance, line[i].row + m.row * distance});
    }
    return out;
}

void draw_rain_lines(vtpen::Drawing& drawing, const RainContours& contours, int cols, int rows, int cell_w, int cell_h,
                     const Blocked& blocked, bool white) {
    const DashSizes sizes = dash_sizes(cell_w);
    std::vector<std::vector<CellPoint>> pieces;
    for (int band = 0; band < RAIN_BANDS; band++) {                      // light first: the heavier lines go on top
        if (contours.band[band].empty()) continue;
        const RainStyle style = rain_style(band, white);
        drawing.set_colour({style.r, style.g, style.b});
        drawing.set_thickness(1);
        for (const ContourLine& line : contours.band[band]) {
            std::vector<CellPoint> cells = line.points;
            if (line.closed && !cells.empty()) cells.push_back(cells.front());
            pieces.clear();
            clip_to_cells(cells, cols, rows, blocked, pieces);
            for (const std::vector<CellPoint>& piece : pieces) {
                std::vector<CellPoint> px;
                px.reserve(piece.size());
                for (const CellPoint& p : piece) px.push_back({p.col * cell_w, p.row * cell_h});
                auto emit = [&](const std::vector<CellPoint>& l) {
                    std::vector<vtpen::Point> pts;
                    pts.reserve(l.size());
                    for (const CellPoint& p : l) pts.push_back({p.col, p.row});
                    if (pts.size() >= 2) drawing.polyline(pts);
                };
                switch (style.line) {
                    case LineStyle::Solid: emit(px); break;
                    case LineStyle::Dotted: for (const auto& d : dash_line(px, sizes.dot_on, sizes.dot_off)) emit(d); break;
                    case LineStyle::Dashed: for (const auto& d : dash_line(px, sizes.dash_on, sizes.dash_off)) emit(d); break;
                    case LineStyle::Doubled:
                        emit(offset_line(px, sizes.double_gap / 2));
                        emit(offset_line(px, -sizes.double_gap / 2));
                        break;
                }
            }
        }
    }
}

void draw_rain_tints(const CellGrid& grid, const RadarFrame& frame, vtscreen::Picture& picture) {
    if (frame.width <= 0) return;
    for (int row = 0; row < grid.rows() && row < picture.rows; row++)
        for (int col = 0; col < grid.cols() && col < picture.cols; col++) {
            const int band = rain_band_of(frame.strongest_in(grid.box(col, row)));
            if (band < 0) continue;
            const RainTint tint = rain_tint(band);
            vtscreen::PicCell& cell = picture.at(row, col);
            cell.alt_lum = 1.0;
            cell.alt_r = tint.r; cell.alt_g = tint.g; cell.alt_b = tint.b;
            if (!cell.text && cell.lum <= 0.1) cell.lum = 0;                      // the water's faint letters would show on the tint: blank
        }
}

}  // namespace vtw
