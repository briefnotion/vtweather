// Tests for the pen map: cutting lines to the screen and around text (checked against a plain sampling of the line, on random
// lines and cells), making the drawing from made-up shapes with known answers and from the real map file, how often it is made
// again, which lines are there at which zoom, and what the terminal's answers lead to (ReGIS if it covers the map, else Sixel).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <string>

#include "check.h"

#include "../src/basemap.h"
#include "../src/draw_ground.h"
#include "../src/ground_map.h"
#include "../src/input.h"
#include "../src/pen_map.h"
#include "../src/pen_support.h"

#ifndef VTW_DATA_DIR
#error "VTW_DATA_DIR must name the data folder"
#endif

namespace {

using namespace vtw;

// ---- helpers ----

Blocked clear_cells(int cols, int rows) { return Blocked((size_t)cols * rows, 0); }

// The distance from a point to a segment.
double distance_to_segment(CellPoint p, CellPoint a, CellPoint b) {
    const double dx = b.col - a.col, dy = b.row - a.row, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.col - a.col) * dx + (p.row - a.row) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(p.col - (a.col + t * dx), p.row - (a.row + t * dy));
}

double distance_to_pieces(CellPoint p, const std::vector<std::vector<CellPoint>>& pieces) {
    double best = 1e9;
    for (const auto& piece : pieces)
        for (size_t i = 1; i < piece.size(); i++) best = std::min(best, distance_to_segment(p, piece[i - 1], piece[i]));
    return best;
}

double length_of(const std::vector<std::vector<CellPoint>>& pieces) {
    double n = 0;
    for (const auto& piece : pieces)
        for (size_t i = 1; i < piece.size(); i++) n += std::hypot(piece[i].col - piece[i - 1].col, piece[i].row - piece[i - 1].row);
    return n;
}

// Is a position inside the cells and not in a blocked cell? Positions close to a cell edge are not decided (an edge belongs
// to neither cell), so they answer "unsure".
enum class Where { Visible, Hidden, Unsure };
Where classify(CellPoint p, int cols, int rows, const Blocked& blocked) {
    const double eps = 1e-6;
    if (p.col < -eps || p.row < -eps || p.col > cols + eps || p.row > rows + eps) return Where::Hidden;
    const double fc = p.col - std::floor(p.col), fr = p.row - std::floor(p.row);
    if (fc < eps || fc > 1 - eps || fr < eps || fr > 1 - eps) return Where::Unsure;
    const int col = (int)std::floor(p.col), row = (int)std::floor(p.row);
    if (col < 0 || col >= cols || row < 0 || row >= rows) return Where::Hidden;
    return blocked[(size_t)row * cols + col] ? Where::Hidden : Where::Visible;
}

// What drawing a Drawing on a canvas gives, hard edges so a colour is exact where a line is.
vtpen::Canvas render(const vtpen::Drawing& drawing, int w, int h) {
    vtpen::Canvas canvas(w, h, drawing.width(), drawing.height());
    canvas.set_edge_levels(1);
    drawing.draw(canvas);
    return canvas;
}

int count_colour(const vtpen::Canvas& canvas, vtpen::Rgb colour) {
    int n = 0;
    for (int y = 0; y < canvas.height(); y++)
        for (int x = 0; x < canvas.width(); x++) n += canvas.pixel(x, y) == colour;
    return n;
}
int count_ink(const vtpen::Canvas& canvas) {
    int n = 0;
    for (int y = 0; y < canvas.height(); y++)
        for (int x = 0; x < canvas.width(); x++) n += canvas.pixel(x, y) != canvas.background();
    return n;
}
vtpen::Rgb rgb_of(PenStyle s) { return {s.r, s.g, s.b}; }

int32_t units(double degrees) { return (int32_t)std::lround(degrees * MAP_UNITS_PER_DEGREE); }

MapPolygon polygon_of_square(double south, double west, double north, double east) {
    MapPolygon p;
    p.box = GeoBox{south, west, north, east};
    p.rings.push_back({units(west), units(south), units(east), units(south), units(east), units(north), units(west), units(north)});
    return p;
}

MapLine line_of(std::vector<std::pair<double, double>> lon_lat) {      // (lon, lat) pairs
    MapLine line;
    double south = 90, north = -90, west = 180, east = -180;
    for (const auto& p : lon_lat) {
        line.points.push_back(units(p.first));
        line.points.push_back(units(p.second));
        west = std::min(west, p.first); east = std::max(east, p.first);
        south = std::min(south, p.second); north = std::max(north, p.second);
    }
    line.box = GeoBox{south, west, north, east};
    return line;
}

}  // namespace

int main() {
    // ---- cutting a line to the cells and around blocked cells ----
    {
        const int cols = 10, rows = 6;
        std::vector<std::vector<CellPoint>> pieces;
        const Blocked clear = clear_cells(cols, rows);

        // a line wholly inside comes back as it is
        clip_to_cells({{1, 1}, {4, 2}, {8, 5}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 1 && pieces[0].size() == 3);
        CHECK(pieces.size() == 1 && pieces[0][0].col == 1 && pieces[0][2].row == 5);

        // a line that leaves the screen is cut at the edge
        pieces.clear();
        clip_to_cells({{-5, 3}, {5, 3}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 1 && pieces[0].size() == 2);
        CHECK(pieces.size() == 1 && std::fabs(pieces[0][0].col - 0) < 1e-9 && std::fabs(pieces[0][1].col - 5) < 1e-9);
        pieces.clear();
        clip_to_cells({{-5, 3}, {-1, 3}}, cols, rows, clear, pieces);               // wholly outside: nothing
        CHECK(pieces.empty());
        pieces.clear();
        clip_to_cells({{5, -3}, {5, 20}}, cols, rows, clear, pieces);               // across, top to bottom
        CHECK(pieces.size() == 1 && std::fabs(length_of(pieces) - rows) < 1e-9);

        // a blocked cell in the way: two pieces, ending at its edges
        Blocked one = clear_cells(cols, rows);
        one[(size_t)3 * cols + 4] = 1;                                               // column 4, row 3
        pieces.clear();
        clip_to_cells({{0.5, 3.5}, {9.5, 3.5}}, cols, rows, one, pieces);
        CHECK(pieces.size() == 2);
        CHECK(pieces.size() == 2 && std::fabs(pieces[0].back().col - 4) < 1e-9 && std::fabs(pieces[1].front().col - 5) < 1e-9);
        CHECK(pieces.size() == 2 && std::fabs(pieces[0].front().col - 0.5) < 1e-9 && std::fabs(pieces[1].back().col - 9.5) < 1e-9);

        // a line beside the blocked cell is untouched, a line along a row of blocked cells is gone
        pieces.clear();
        clip_to_cells({{0.5, 2.5}, {9.5, 2.5}}, cols, rows, one, pieces);
        CHECK(pieces.size() == 1 && std::fabs(length_of(pieces) - 9) < 1e-9);
        Blocked row_blocked = clear_cells(cols, rows);
        for (int c = 0; c < cols; c++) row_blocked[(size_t)3 * cols + c] = 1;
        pieces.clear();
        clip_to_cells({{0.5, 3.5}, {9.5, 3.5}}, cols, rows, row_blocked, pieces);
        CHECK(pieces.empty());

        // a diagonal through the corner between four cells, two of them blocked
        Blocked corner = clear_cells(cols, rows);
        corner[(size_t)2 * cols + 3] = 1; corner[(size_t)3 * cols + 2] = 1;          // the cells on the other diagonal
        pieces.clear();
        clip_to_cells({{0, 0}, {6, 6}}, cols, rows, corner, pieces);
        CHECK(pieces.size() == 1 && std::fabs(length_of(pieces) - 6 * std::sqrt(2.0)) < 1e-9);

        // lines that run along or past the outside of the screen, or cut its corner without entering it, are gone
        for (const auto& l : std::vector<std::vector<CellPoint>>{{{0, -2}, {10, -2}}, {{0, rows + 2}, {10, rows + 2}}, {{-2, 0}, {-2, 6}}, {{cols + 2, 0}, {cols + 2, 6}},
                                                                    {{-3, 2}, {2, -3}}, {{cols + 3, rows - 2}, {cols - 2, rows + 3}}, {{-1, rows + 0.5}, {1, rows + 2}},
                                                                    {{0, -0.5}, {10, -0.5}}, {{-0.5, 0}, {-0.5, 6}}, {{0, rows + 0.5}, {10, rows + 0.5}}, {{cols + 0.5, 0}, {cols + 0.5, 6}}}) {
            pieces.clear();
            clip_to_cells(l, cols, rows, clear, pieces);
            CHECK(pieces.empty());
        }
        // a line along the very edge of the screen is on it (a line along a cell's edge belongs to the cell it is in)
        pieces.clear();
        clip_to_cells({{0, 0}, {cols, 0}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 1 && std::fabs(length_of(pieces) - cols) < 1e-9);
        // a line that is not a number ends there
        pieces.clear();
        clip_to_cells({{1, 1}, {3, 1}, {std::nan(""), 1}, {6, 1}, {8, 1}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 2);
        // a line in several segments stays one line through its vertices
        pieces.clear();
        clip_to_cells({{1, 1}, {3, 1}, {3, 4}, {6, 4}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 1 && pieces[0].size() == 4);
        // a wrong sized mask or no cells: nothing, and no crash
        pieces.clear();
        clip_to_cells({{1, 1}, {3, 1}}, cols, rows, Blocked(3, 0), pieces);
        clip_to_cells({{1, 1}, {3, 1}}, 0, 0, Blocked(), pieces);
        clip_to_cells({}, cols, rows, clear, pieces);
        clip_to_cells({{2, 2}}, cols, rows, clear, pieces);
        CHECK(pieces.empty());
        // points far outside (a map line that runs for thousands of cells) cost nothing and keep the part inside
        pieces.clear();
        clip_to_cells({{-1e9, 3}, {1e9, 3}}, cols, rows, clear, pieces);
        CHECK(pieces.size() == 1 && std::fabs(length_of(pieces) - cols) < 1e-6);
    }

    // ---- random lines and masks, against a plain sampling of the line ----
    {
        std::mt19937 rng(5);
        auto uniform = [&](double lo, double hi) { return lo + (hi - lo) * ((double)(rng() % 1000001) / 1000000.0); };
        long checked = 0, wrong_visible = 0, wrong_hidden = 0, too_long = 0;
        for (int trial = 0; trial < 400; trial++) {
            const int cols = 3 + (int)(rng() % 20), rows = 2 + (int)(rng() % 12);
            Blocked blocked = clear_cells(cols, rows);
            const int density = (int)(rng() % 60);
            for (uint8_t& b : blocked) b = (int)(rng() % 100) < density;
            std::vector<CellPoint> line;
            const int n = 2 + (int)(rng() % 5);
            for (int i = 0; i < n; i++) line.push_back({uniform(-5, cols + 5), uniform(-5, rows + 5)});
            std::vector<std::vector<CellPoint>> pieces;
            clip_to_cells(line, cols, rows, blocked, pieces);
            // every piece lies on the line, in the cells, in no blocked cell
            for (const auto& piece : pieces) {
                CHECK(piece.size() >= 2);
                for (size_t i = 1; i < piece.size(); i++) {
                    const CellPoint mid{(piece[i].col + piece[i - 1].col) / 2, (piece[i].row + piece[i - 1].row) / 2};
                    const Where w = classify(mid, cols, rows, blocked);
                    if (w == Where::Hidden) wrong_visible++;
                    std::vector<std::vector<CellPoint>> whole{line};
                    if (distance_to_pieces(mid, whole) > 1e-6) too_long++;
                }
            }
            // every sample along the line that is visible is on a piece; every hidden one is not
            for (size_t s = 1; s < line.size(); s++)
                for (int k = 0; k <= 60; k++) {
                    const double t = k / 60.0;
                    const CellPoint p{line[s - 1].col + t * (line[s].col - line[s - 1].col), line[s - 1].row + t * (line[s].row - line[s - 1].row)};
                    const Where w = classify(p, cols, rows, blocked);
                    if (w == Where::Unsure) continue;
                    checked++;
                    const double d = distance_to_pieces(p, pieces);
                    if (w == Where::Visible && d > 1e-6) wrong_hidden++;
                    if (w == Where::Hidden && d < 1e-6) wrong_visible++;
                }
        }
        CHECK(checked > 20000);
        CHECK(wrong_visible == 0);
        CHECK(wrong_hidden == 0);
        CHECK(too_long == 0);
    }

    // ---- which cells have text in them ----
    {
        vtscreen::Picture picture;
        picture.init(5, 8);                                                         // 5 rows of map and the bar row
        vtscreen::PicCell text;
        text.text = true; text.ch = U'x';
        picture.set(0, 0, text); picture.set(2, 7, text); picture.set(4, 3, text);
        picture.set(5, 2, text);                                                    // the bar row: not part of the map
        vtscreen::PicCell shimmer;
        shimmer.lum = 0.5; shimmer.shimmer = 1;                                      // a letter the Display chooses: not text
        picture.set(1, 1, shimmer);
        const Blocked blocked = blocked_cells(picture, 8, 5);
        CHECK(blocked.size() == 40);
        int n = 0;
        for (uint8_t b : blocked) n += b;
        CHECK(n == 3 && blocked[0] == 1 && blocked[(size_t)2 * 8 + 7] == 1 && blocked[(size_t)4 * 8 + 3] == 1 && blocked[(size_t)1 * 8 + 1] == 0);
        CHECK(blocked_cells(picture, 20, 20).size() == 400);                         // asked for more than the picture has: the rest is clear
    }

    // ---- made-up shapes with known answers ----
    {
        // a square of land 0.4 degrees tall and 0.6 wide around (40N, 100W), as the only land, with a cut edge on its south side
        BaseMap map;
        MapLayer land;
        land.kind = LayerKind::Land; land.detail = 2;
        land.coverage = GeoBox{39.8, -105, 45, -95};                                // the file covers 39.8N and up: that edge is a cut
        land.polygons.push_back(polygon_of_square(39.8, -100.3, 40.2, -99.7));      // its south side lies on 39.8N, the cut
        map.layers.push_back(land);                                                  // the only layer: it is used whatever the zoom, though it does not cover the screen
        View view;
        view.set_cell_aspect(2.0);
        view.set_size(100, 40);
        view.set_centre({40, -100});
        view.set_zoom_km(300);
        const int cw = 8, ch = 16;
        const Blocked none = clear_cells(100, 40);
        auto drawing = make_pen_map(map, view, cw, ch, none);
        CHECK(drawing != nullptr && drawing->width() == 100 * cw && drawing->height() == 40 * ch);
        const vtpen::Canvas canvas = render(*drawing, 100 * cw, 40 * ch);
        const vtpen::Rgb coast = rgb_of(pen_style_coast());
        CHECK(count_colour(canvas, coast) > 300);                                    // the outline is there
        // where the square's four corners are on the screen
        double c_nw, r_nw, c_ne, r_ne, c_sw, r_sw, c_se, r_se;
        view.to_cell({40.2, -100.3}, c_nw, r_nw); view.to_cell({40.2, -99.7}, c_ne, r_ne);
        view.to_cell({39.8, -100.3}, c_sw, r_sw); view.to_cell({39.8, -99.7}, c_se, r_se);
        const int x_w = (int)std::lround(c_nw * cw), x_e = (int)std::lround(c_ne * cw);
        const int y_n = (int)std::lround(r_nw * ch), y_s = (int)std::lround(r_sw * ch);
        auto ink_near = [&](int x, int y) {
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (canvas.pixel(x + dx, y + dy) == coast) return true;
            return false;
        };
        const int x_mid = (x_w + x_e) / 2, y_mid = (y_n + y_s) / 2;
        CHECK(ink_near(x_mid, y_n));                                                 // north side
        CHECK(ink_near(x_w, y_mid) && ink_near(x_e, y_mid));                         // west and east sides
        // the only level there is is the one used; its south side is the edge of what it covers: where the file was cut
        CHECK(choose_detail(map, view) == 2);
        view.set_zoom_km(120);
        auto fine = make_pen_map(map, view, cw, ch, none);
        CHECK(choose_detail(map, view) == 2);
        const vtpen::Canvas fine_canvas = render(*fine, 100 * cw, 40 * ch);
        double c0, r0, c1, r1, cs, rs;
        view.to_cell({40.2, -100.3}, c0, r0); view.to_cell({40.2, -99.7}, c1, r1); view.to_cell({39.8, -100}, cs, rs);
        const int fx = (int)std::lround((c0 + c1) / 2 * cw), fy_north = (int)std::lround(r0 * ch), fy_south = (int)std::lround(rs * ch);
        bool north_ink = false, south_ink = false;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            north_ink |= fine_canvas.pixel(fx + dx, fy_north + dy) == coast;
            south_ink |= fine_canvas.pixel(fx + dx, fy_south + dy) == coast;
        }
        CHECK(north_ink);                                                            // a real coast
        CHECK(!south_ink);                                                           // along the edge of what the file covers: not a coast
        // the same for each of the four sides: a side along the edge of what the layer covers is a cut, and the others are not
        for (int side = 0; side < 4; side++) {
            BaseMap one;
            MapLayer layer;
            layer.kind = LayerKind::Land; layer.detail = 2;
            layer.coverage = GeoBox{39.8 - (side == 0 ? 0 : 1), -100.3 - (side == 1 ? 0 : 1), 40.2 + (side == 2 ? 0 : 1), -99.7 + (side == 3 ? 0 : 1)};
            layer.polygons.push_back(polygon_of_square(39.8, -100.3, 40.2, -99.7));
            one.layers.push_back(layer);
            View v;
            v.set_cell_aspect(2.0); v.set_size(100, 40); v.set_centre({40, -100}); v.set_zoom_km(120);
            const vtpen::Canvas c = render(*make_pen_map(one, v, cw, ch, none), 100 * cw, 40 * ch);
            double wcol, wrow, ecol, erow, ncol, nrow, scol, srow;
            v.to_cell({40, -100.3}, wcol, wrow); v.to_cell({40, -99.7}, ecol, erow);
            v.to_cell({40.2, -100}, ncol, nrow); v.to_cell({39.8, -100}, scol, srow);
            const int mid_x = (int)std::lround((wcol + ecol) / 2 * cw), mid_y = (int)std::lround((nrow + srow) / 2 * ch);
            auto near_ink = [&](int x, int y) { bool k = false; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) k |= c.pixel(x + dx, y + dy) == coast; return k; };
            const bool has[4] = {near_ink(mid_x, (int)std::lround(srow * ch)), near_ink((int)std::lround(wcol * cw), mid_y),
                                 near_ink(mid_x, (int)std::lround(nrow * ch)), near_ink((int)std::lround(ecol * cw), mid_y)};
            for (int k = 0; k < 4; k++) CHECK(has[k] == (k != side));              // 0 south, 1 west, 2 north, 3 east
        }
        // a lake in the land has a shore too
        {
            BaseMap with_lake;
            MapLayer land2;
            land2.kind = LayerKind::Land; land2.detail = 2; land2.coverage = GeoBox{-90, -180, 90, 180};
            land2.polygons.push_back(polygon_of_square(39.5, -101, 40.5, -99));
            with_lake.layers.push_back(land2);
            MapLayer lake;
            lake.kind = LayerKind::Lake; lake.detail = 2; lake.coverage = land2.coverage;
            lake.polygons.push_back(polygon_of_square(39.9, -100.1, 40.1, -99.9));
            with_lake.layers.push_back(lake);
            View v;
            v.set_cell_aspect(2.0); v.set_size(100, 40); v.set_centre({40, -100}); v.set_zoom_km(120);
            const vtpen::Canvas c = render(*make_pen_map(with_lake, v, cw, ch, none), 100 * cw, 40 * ch);
            double lc, lr; v.to_cell({40.1, -100}, lc, lr);                          // the middle of the lake's north shore
            bool shore = false;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) shore |= c.pixel((int)std::lround(lc * cw) + dx, (int)std::lround(lr * ch) + dy) == coast;
            CHECK(shore);
        }
        // a blocked cell is kept clear: block the cells round the north side's middle
        Blocked mask = clear_cells(100, 40);
        const int bc = (int)std::floor((c0 + c1) / 2), br = (int)std::floor(r0);
        for (int dr = -1; dr <= 1; dr++) for (int dc = -2; dc <= 2; dc++)
            if (br + dr >= 0 && br + dr < 40) mask[(size_t)(br + dr) * 100 + bc + dc] = 1;
        auto cut = make_pen_map(map, view, cw, ch, mask);
        const vtpen::Canvas cut_canvas = render(*cut, 100 * cw, 40 * ch);
        bool ink_in_blocked = false;
        for (int dr = -1; dr <= 1; dr++) for (int dc = -2; dc <= 2; dc++)
            for (int y = (br + dr) * ch + 1; y < (br + dr + 1) * ch - 1; y++)
                for (int x = (bc + dc) * cw + 1; x < (bc + dc + 1) * cw - 1; x++) ink_in_blocked |= cut_canvas.pixel(x, y) == coast;
        CHECK(!ink_in_blocked);
        CHECK(count_colour(cut_canvas, coast) > 100 && count_colour(cut_canvas, coast) < count_colour(fine_canvas, coast));   // and the rest is still there
        // a map with nothing in it, and a view with no cells or no cell size
        CHECK(make_pen_map(BaseMap(), view, cw, ch, none) != nullptr);
        CHECK(count_ink(render(*make_pen_map(BaseMap(), view, cw, ch, none), 100 * cw, 40 * ch)) == 0);
        CHECK(make_pen_map(map, view, 0, ch, none) == nullptr && make_pen_map(map, view, cw, 0, none) == nullptr);
        View tiny;
        tiny.set_size(1, 1);                                                         // one cell: nothing much to draw, and no trouble
        CHECK(make_pen_map(map, tiny, cw, ch, clear_cells(1, 1)) != nullptr);
        // a mask of the wrong size is as if nothing were blocked
        const vtpen::Canvas wrong_mask = render(*make_pen_map(map, view, cw, ch, Blocked(7, 1)), 100 * cw, 40 * ch);
        CHECK(count_colour(wrong_mask, coast) == count_colour(fine_canvas, coast));
    }

    // ---- lines: each kind in its own colour, and which are there at which zoom ----
    {
        BaseMap map;
        auto add_lines = [&](LayerKind kind, int detail, std::vector<MapLine> lines) {
            MapLayer layer;
            layer.kind = kind; layer.detail = detail;
            layer.coverage = GeoBox{-90, -180, 90, 180};
            layer.lines = std::move(lines);
            map.layers.push_back(layer);
        };
        MapLayer land;
        land.kind = LayerKind::Land; land.coverage = GeoBox{-90, -180, 90, 180};
        for (int detail = 0; detail < DETAIL_LEVELS; detail++) { land.detail = detail; map.layers.push_back(land); }
        // six parallel lines across the middle, one of each kind, 0.2 degrees apart
        const double lats[] = {40.5, 40.3, 40.1, 39.9, 39.7, 39.5};
        const LayerKind kinds[] = {LayerKind::River, LayerKind::Road, LayerKind::Highway, LayerKind::State, LayerKind::Country, LayerKind::River};
        for (int d = 0; d < DETAIL_LEVELS; d++) {
            add_lines(LayerKind::River, d, {line_of({{-101, lats[0]}, {-99, lats[0]}})});
            add_lines(LayerKind::State, d, {line_of({{-101, lats[3]}, {-99, lats[3]}})});
            add_lines(LayerKind::Country, d, {line_of({{-101, lats[4]}, {-99, lats[4]}})});
        }
        add_lines(LayerKind::Road, DETAIL_LEVELS - 1, {line_of({{-101, lats[1]}, {-99, lats[1]}})});
        add_lines(LayerKind::Highway, DETAIL_LEVELS - 1, {line_of({{-101, lats[2]}, {-99, lats[2]}})});
        (void)kinds;
        auto make = [&](double km) {
            View view;
            view.set_cell_aspect(2.0);
            view.set_size(120, 50);
            view.set_centre({40, -100});
            view.set_zoom_km(km);
            return make_pen_map(map, view, 8, 16, clear_cells(120, 50));
        };
        auto colours_at = [&](double km) {
            struct Seen { bool river, road, highway, state, country; } seen;
            auto drawing = make(km);
            const vtpen::Canvas canvas = render(*drawing, 120 * 8, 50 * 16);
            seen.river = count_colour(canvas, rgb_of(pen_style(LayerKind::River))) > 0;
            seen.road = count_colour(canvas, rgb_of(pen_style(LayerKind::Road))) > 0;
            seen.highway = count_colour(canvas, rgb_of(pen_style(LayerKind::Highway))) > 0;
            seen.state = count_colour(canvas, rgb_of(pen_style(LayerKind::State))) > 0;
            seen.country = count_colour(canvas, rgb_of(pen_style(LayerKind::Country))) > 0;
            return seen;
        };
        // 200 km across: the lines 0.2 degrees (22 km) apart are all on screen; highways and roads are close enough
        auto a = colours_at(150);
        CHECK(a.river && a.road && a.highway && a.state && a.country);
        auto b = colours_at(600);                                                    // highways yes (< 1000), other roads no (>= 250)
        CHECK(!b.road && b.highway && b.river && b.state && b.country);
        auto c = colours_at(1200);                                                   // neither (>= 1000)
        CHECK(!c.road && !c.highway && c.river && c.state && c.country);
        // each kind has its own colour
        const PenStyle all[] = {pen_style(LayerKind::River), pen_style(LayerKind::Road), pen_style(LayerKind::Highway),
                                pen_style(LayerKind::State), pen_style(LayerKind::Country), pen_style_coast()};
        for (size_t i = 0; i < 6; i++)
            for (size_t j = i + 1; j < 6; j++) CHECK(!(all[i].r == all[j].r && all[i].g == all[j].g && all[i].b == all[j].b));
        for (const PenStyle& s : all) CHECK(s.thickness >= 1 && s.thickness <= 3);
    }

    // ---- the real map ----
    {
        BaseMap map;
        std::string problem;
        const std::string file = find_basemap_file("");
        CHECK(!file.empty() && load_basemap(file, map, problem));
        View view;
        view.set_cell_aspect(2.0);
        view.set_size(160, 48);
        const int cw = 13, ch = 26;
        struct Place { const char* name; GeoPoint at; double km; };
        const Place places[] = {{"Seattle 150 km", {47.6, -122.3}, 150}, {"Wichita 80 km", {37.7, -97.3}, 80}, {"Chicago 400 km", {41.9, -87.7}, 400},
                                {"central US 3000 km", {39, -98}, 3000}, {"world 6000 km", {20, 10}, 6000}, {"Miami 500 km", {26, -80}, 500}};
        for (const Place& p : places) {
            view.set_centre(p.at);
            view.set_zoom_km(p.km);
            const auto t0 = std::chrono::steady_clock::now();
            auto drawing = make_pen_map(map, view, cw, ch, clear_cells(160, 48));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            CHECK(drawing != nullptr);
            const vtpen::Canvas canvas = render(*drawing, 160 * cw, 48 * ch);
            const int ink = count_ink(canvas);
            std::printf("  %s: %d ink pixels, %.1f ms\n", p.name, ink, ms);
            CHECK(ink > 200);                                                        // something is drawn everywhere we look
            CHECK(ms < 250);
        }
        // a coast on screen near Seattle: the colour is there; at Wichita, far from any sea, there is none (a state border is)
        view.set_centre({47.6, -122.3}); view.set_zoom_km(150);
        CHECK(count_colour(render(*make_pen_map(map, view, cw, ch, clear_cells(160, 48)), 160 * cw, 48 * ch), rgb_of(pen_style_coast())) > 500);
        view.set_centre({37.7, -97.3}); view.set_zoom_km(80);
        const vtpen::Canvas wichita = render(*make_pen_map(map, view, cw, ch, clear_cells(160, 48)), 160 * cw, 48 * ch);
        CHECK(count_colour(wichita, rgb_of(pen_style_coast())) == 0);
        CHECK(count_colour(wichita, rgb_of(pen_style(LayerKind::Highway))) > 200);   // I-35 and the Kansas Turnpike
        CHECK(count_colour(wichita, rgb_of(pen_style(LayerKind::River))) > 100);     // the Arkansas river
        // no ink where the file was cut: at the edge of the map file (lat 5, lon -170) the view shows no straight cut line
        view.set_centre({7, -100}); view.set_zoom_km(1500);
        const vtpen::Canvas south = render(*make_pen_map(map, view, cw, ch, clear_cells(160, 48)), 160 * cw, 48 * ch);
        double ccol, crow; view.to_cell({5, -100}, ccol, crow);
        int ink_on_cut_row = 0;
        if (crow >= 1 && crow < 47)
            for (int x = 0; x < south.width(); x++) ink_on_cut_row += south.pixel(x, (int)std::lround(crow * ch)) == rgb_of(pen_style_coast());
        CHECK(ink_on_cut_row < south.width() / 20);                                  // a few crossings, not a line the whole way
    }

    // ---- without colour: the same lines, all white ----
    {
        BaseMap map;
        std::string problem;
        load_basemap(find_basemap_file(""), map, problem);
        View view;
        view.set_cell_aspect(2.0);
        view.set_size(100, 30);
        const struct { GeoPoint at; double km; } views[] = {{{47.6, -122.3}, 150}, {{37.7, -97.3}, 80}, {{39, -98}, 3000}};
        for (const auto& v : views) {
            view.set_centre(v.at);
            view.set_zoom_km(v.km);
            const Blocked none = clear_cells(100, 30);
            const vtpen::Canvas coloured = render(*make_pen_map(map, view, 13, 26, none, false), 100 * 13, 30 * 26);
            const vtpen::Canvas white = render(*make_pen_map(map, view, 13, 26, none, true), 100 * 13, 30 * 26);
            const int ink = count_ink(white);
            CHECK(ink > 200);
            CHECK(count_colour(white, {255, 255, 255}) == ink);                      // every line is white
            CHECK(count_ink(coloured) == ink);                                       // and they are the same lines
            bool same_places = true;
            for (int y = 0; y < white.height() && same_places; y += 3)
                for (int x = 0; x < white.width(); x += 3)
                    if ((white.pixel(x, y) != white.background()) != (coloured.pixel(x, y) != coloured.background())) { same_places = false; break; }
            CHECK(same_places);
        }
        // the drawing is made again when colour is switched, and not otherwise
        PenMap pen(map);
        vtscreen::Picture picture;
        view.set_centre({47.6, -122.3}); view.set_zoom_km(150);
        picture.init(view.rows(), view.cols());
        pen.update(view, picture, 13, 26, 0.0, true, false);
        CHECK(pen.made() == 1);
        picture.init(view.rows(), view.cols());
        pen.update(view, picture, 13, 26, 0.01, true, false);
        CHECK(pen.made() == 1);
        picture.init(view.rows(), view.cols());
        pen.update(view, picture, 13, 26, 0.02, true, true);                         // at once, however soon
        CHECK(pen.made() == 2);
        picture.init(view.rows(), view.cols());
        pen.update(view, picture, 13, 26, 0.03, true, true);
        CHECK(pen.made() == 2);
    }

    // ---- how often it is made again ----
    {
        BaseMap map;
        std::string problem;
        load_basemap(find_basemap_file(""), map, problem);
        PenMap pen(map);
        View view;
        view.set_cell_aspect(2.0);
        view.set_size(100, 30);
        view.set_centre({47.6, -122.3});
        view.set_zoom_km(150);
        vtscreen::Picture picture;
        auto fresh_picture = [&]() { picture.init(view.rows(), view.cols()); };
        fresh_picture();
        pen.update(view, picture, 13, 26, 0.0, true);
        CHECK(pen.made() == 1 && picture.vectors.size() == 1);
        CHECK(picture.vectors[0].row == 0 && picture.vectors[0].col == 0 && picture.vectors[0].rows == 30 && picture.vectors[0].cols == 100);
        const auto first = pen.drawing();
        CHECK(picture.vectors[0].drawing == first);
        // nothing changed: the same drawing again, not made again
        fresh_picture();
        pen.update(view, picture, 13, 26, 0.03, true);
        CHECK(pen.made() == 1 && picture.vectors.size() == 1 && picture.vectors[0].drawing == first);
        // the view moves, but too soon after the last one: the old drawing stays; later it is made again
        view.pan_cells(5, 0);
        fresh_picture();
        pen.update(view, picture, 13, 26, 0.05, true);
        CHECK(pen.made() == 1 && picture.vectors[0].drawing == first);
        fresh_picture();
        pen.update(view, picture, 13, 26, 0.2, true);
        CHECK(pen.made() == 2 && picture.vectors[0].drawing != first);
        const auto second = pen.drawing();
        // a letter of the scene's own appears (a card): that is a change, so it is made again (when it is time)
        fresh_picture();
        vtscreen::PicCell text;
        text.text = true; text.ch = U'x';
        picture.set(3, 3, text);
        pen.update(view, picture, 13, 26, 0.25, true);                               // too soon after the last one
        CHECK(pen.made() == 2 && pen.drawing() == second);
        fresh_picture();
        picture.set(3, 3, text);
        pen.update(view, picture, 13, 26, 0.4, true);
        CHECK(pen.made() == 3 && pen.drawing() != second);
        // a change of size is made at once, however soon, and with the right size
        view.set_size(110, 32);
        fresh_picture();
        pen.update(view, picture, 13, 26, 0.41, true);
        CHECK(pen.made() == 4 && picture.vectors[0].cols == 110 && picture.vectors[0].rows == 32 && pen.drawing()->width() == 110 * 13);
        // a change of the cell size too
        fresh_picture();
        pen.update(view, picture, 10, 20, 0.42, true);
        CHECK(pen.made() == 5 && pen.drawing()->width() == 110 * 10);
        // turned off: nothing on the picture, and the drawing is gone; on again: made again
        fresh_picture();
        pen.update(view, picture, 10, 20, 1.0, false);
        CHECK(picture.vectors.empty() && pen.drawing() == nullptr);
        fresh_picture();
        pen.update(view, picture, 10, 20, 1.1, true);
        CHECK(pen.made() == 6 && picture.vectors.size() == 1);
        fresh_picture();
        pen.update(view, picture, 0, 0, 1.2, true);                                  // no cell size: nothing
        CHECK(picture.vectors.empty());
    }

    // ---- the water alone when the lines are the pen's ----
    {
        BaseMap map;
        std::string problem;
        load_basemap(find_basemap_file(""), map, problem);
        View view;
        view.set_cell_aspect(2.0);
        view.set_size(100, 30);
        view.set_centre({47.6, -122.3});
        view.set_zoom_km(150);
        GroundMap ground(map);
        ground.update(view);
        vtscreen::Picture with, without;
        with.init(30, 100); without.init(30, 100);
        LayerBlend blend_with(with), blend_without(without);
        draw_ground(ground, with, blend_with, true);
        draw_ground(ground, without, blend_without, false);
        int water = 0, lines_in_with = 0, lines_in_without = 0;
        for (int row = 0; row < 30; row++)
            for (int col = 0; col < 100; col++) {
                const vtscreen::PicCell& a = with.cell[(size_t)row * with.cols + col];
                const vtscreen::PicCell& b = without.cell[(size_t)row * without.cols + col];
                if (ground.at(col, row) == Ground::Water) { water++; CHECK(a == b); }
                else { lines_in_with += a.lum > 0; lines_in_without += b.lum > 0; }
            }
        CHECK(water > 200);
        CHECK(lines_in_with > 50 && lines_in_without == 0);
    }

    // ---- what the terminal's answers lead to ----
    {
        auto caps = [](bool regis, bool sixel, int cell_w, int cell_h, int max_w, int max_h, int win_w, int win_h) {
            vtpen::Capabilities c;
            c.answered = true; c.regis = regis; c.sixel = sixel;
            c.cell_w = cell_w; c.cell_h = cell_h; c.max_width = max_w; c.max_height = max_h; c.window_w = win_w; c.window_h = win_h;
            return c;
        };
        // ReGIS, and its screen (the smaller of the biggest picture and the window) covers a 100 x 30 cell map of 13 x 26 pixel cells
        PenSupport s = pen_support_from(caps(true, true, 13, 26, 1300, 806, 1300, 832), 100, 30);
        CHECK(s.kind == PenKind::Regis && s.cell_w == 13 && s.surface_w == 1300 && s.surface_h == 806);
        CHECK(pen_covers(s, 100, 30) && !pen_covers(s, 101, 30) && !pen_covers(s, 100, 32));
        // it does not cover (xterm's stock limit 1000 x 1000): Sixel if there is Sixel, else nothing
        s = pen_support_from(caps(true, true, 13, 26, 1000, 1000, 1300, 832), 100, 30);
        CHECK(s.kind == PenKind::Sixel && s.cell_w == 13 && pen_covers(s, 5000, 5000));
        s = pen_support_from(caps(true, false, 13, 26, 1000, 1000, 1300, 832), 100, 30);
        CHECK(s.kind == PenKind::None && !pen_covers(s, 1, 1));
        // Sixel only
        CHECK(pen_support_from(caps(false, true, 10, 20, 0, 0, 800, 600), 80, 24).kind == PenKind::Sixel);
        // nothing: no ReGIS and no Sixel; or an unanswered terminal; or no cell size
        CHECK(pen_support_from(caps(false, false, 10, 20, 0, 0, 800, 600), 80, 24).kind == PenKind::None);
        vtpen::Capabilities silent = caps(true, true, 10, 20, 800, 480, 800, 600);
        silent.answered = false;
        CHECK(pen_support_from(silent, 80, 24).kind == PenKind::None);
        CHECK(pen_support_from(caps(true, true, 0, 20, 800, 480, 800, 600), 80, 24).kind == PenKind::None);
        CHECK(pen_support_from(caps(true, true, 10, 0, 800, 480, 800, 600), 80, 24).kind == PenKind::None);
        // ReGIS without a stated maximum is the usual 800 x 480, and never bigger than the window
        s = pen_support_from(caps(true, false, 10, 20, 0, 0, 0, 0), 70, 20);
        CHECK(s.kind == PenKind::Regis && s.surface_w == 800 && s.surface_h == 480);
        s = pen_support_from(caps(true, false, 10, 20, 2000, 2000, 900, 500), 70, 20);
        CHECK(s.kind == PenKind::Regis && s.surface_w == 900 && s.surface_h == 500);
        s = pen_support_from(caps(true, false, 10, 20, 2000, 2000, 900, 500), 91, 20);   // 910 pixels across: more than the window can have
        CHECK(s.kind == PenKind::None);
        // exactly covering is covering
        s = pen_support_from(caps(true, false, 10, 20, 800, 480, 800, 480), 80, 24);
        CHECK(s.kind == PenKind::Regis && pen_covers(s, 80, 24) && !pen_covers(s, 81, 24) && !pen_covers(s, 80, 25));
        // a default one covers nothing
        CHECK(!pen_covers(PenSupport(), 1, 1));
    }

    // ---- the key ----
    {
        CHECK(command_for_key('p') == Command::TogglePen && command_for_key('P') == Command::TogglePen);
        UiState ui;
        View view;
        Settings settings;
        CHECK(ui.pen);
        apply_command(Command::TogglePen, view, settings, ui);
        CHECK(!ui.pen);
        apply_command(Command::TogglePen, view, settings, ui);
        CHECK(ui.pen);
    }

    if (failures == 0) std::printf("all pen map tests passed\n");
    return failures ? 1 : 0;
}
