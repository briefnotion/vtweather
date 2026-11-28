// Tests for the rain's contour lines: the bands, following a level through a lattice (marching squares, checked on made-up
// fields with known answers and on random ones), sampling the radar onto the lattice, rounding and tidying the lines, the line
// styles (dashes, doubled lines), drawing them without text in the way, the tints, how the pen map uses them, and the key to the rain.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>

#include "check.h"

#include "../src/card.h"
#include "../src/cards.h"
#include "../src/cell_grid.h"
#include "../src/pen_map.h"
#include "../src/rain_contours.h"
#include "../src/rain_pen.h"
#include "../src/rain_style.h"

#ifndef VTW_TEST_DATA_DIR
#error "VTW_TEST_DATA_DIR must name the tests/data folder"
#endif
#ifndef VTW_DATA_DIR
#error "VTW_DATA_DIR must name the data folder"
#endif

namespace {

using namespace vtw;

// ---- helpers ----

View make_view(int cols, int rows, double lat, double lon, double km) {
    View v;
    v.set_cell_aspect(2.0);
    v.set_size(cols, rows);
    v.set_centre({lat, lon});
    v.set_zoom_km(km);
    return v;
}

RadarFrame make_frame(GeoBox box, int w, int h, const std::function<int(int, int)>& value) {
    RadarFrame f;
    f.box = box;
    f.width = w; f.height = h;
    f.dbz.resize((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) f.dbz[(size_t)y * w + x] = (uint8_t)std::max(0, std::min(255, value(x, y)));
    return f;
}

RainField field_of(int nx, int ny, double sc, double sr, const std::function<double(int, int)>& value) {
    RainField f;
    f.nx = nx; f.ny = ny; f.step_col = sc; f.step_row = sr;
    f.value.resize((size_t)nx * ny);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) f.value[(size_t)j * nx + i] = (float)value(i, j);
    return f;
}

double distance_to_segment(CellPoint p, CellPoint a, CellPoint b) {
    const double dx = b.col - a.col, dy = b.row - a.row, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.col - a.col) * dx + (p.row - a.row) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(p.col - (a.col + t * dx), p.row - (a.row + t * dy));
}
double distance_to_line(CellPoint p, const std::vector<CellPoint>& line) {
    double best = 1e18;
    for (size_t i = 1; i < line.size(); i++) best = std::min(best, distance_to_segment(p, line[i - 1], line[i]));
    return best;
}
double length_of(const std::vector<CellPoint>& line) {
    double n = 0;
    for (size_t i = 1; i < line.size(); i++) n += std::hypot(line[i].col - line[i - 1].col, line[i].row - line[i - 1].row);
    return n;
}
double area_of(const std::vector<CellPoint>& p, double aspect) {
    double twice = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const CellPoint& a = p[i];
        const CellPoint& b = p[(i + 1) % p.size()];
        twice += a.col * b.row * aspect - b.col * a.row * aspect;
    }
    return std::fabs(twice) / 2;
}
bool inside_polygon(CellPoint p, const std::vector<CellPoint>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if (((poly[i].row > p.row) != (poly[j].row > p.row)) &&
            (p.col < (poly[j].col - poly[i].col) * (p.row - poly[i].row) / (poly[j].row - poly[i].row) + poly[i].col))
            in = !in;
    return in;
}

vtpen::Canvas render(const vtpen::Drawing& drawing, int w, int h) {
    vtpen::Canvas canvas(w, h, drawing.width(), drawing.height());
    canvas.set_edge_levels(1);
    drawing.draw(canvas);
    return canvas;
}
int count_colour(const vtpen::Canvas& c, vtpen::Rgb colour) {
    int n = 0;
    for (int y = 0; y < c.height(); y++) for (int x = 0; x < c.width(); x++) n += c.pixel(x, y) == colour;
    return n;
}
int count_ink(const vtpen::Canvas& c) {
    int n = 0;
    for (int y = 0; y < c.height(); y++) for (int x = 0; x < c.width(); x++) n += c.pixel(x, y) != c.background();
    return n;
}

RadarFrame load_frame(const char* name, GeoBox box) {
    std::ifstream in(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png", std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    Image image;
    std::string error;
    RadarFrame frame;
    CHECK(decode_png(bytes.str(), image, error));
    CHECK(make_radar_frame(image, box, frame, error));
    return frame;
}

}  // namespace

int main() {
    // ---- the bands ----
    {
        CHECK(rain_band_of(0) == -1 && rain_band_of(14.9) == -1 && rain_band_of(-5) == -1);
        CHECK(rain_band_of(15) == 0 && rain_band_of(29.9) == 0);
        CHECK(rain_band_of(30) == 1 && rain_band_of(39.9) == 1);
        CHECK(rain_band_of(40) == 2 && rain_band_of(49.9) == 2);
        CHECK(rain_band_of(50) == 3 && rain_band_of(255) == 3);
        CHECK(rain_band_of(std::nan("")) == -1);
        for (int k = 1; k < RAIN_BANDS; k++) CHECK(RAIN_BAND_MIN_DBZ[k] > RAIN_BAND_MIN_DBZ[k - 1]);    // light to severe
    }

    // ---- following a level through a lattice ----
    {
        // a cone: strength falls off with the distance from the middle, in columns (rows count aspect times as much)
        const int nx = 121, ny = 81;
        const double sc = 0.5, sr = 0.25;
        auto cone = [&](double cx, double cy, double slope, double top) {
            return [=](int i, int j) {
                const double r = std::hypot(i * sc - cx, (j * sr - cy) * 2.0);
                return top - slope * r;
            };
        };
        RainField f = field_of(nx, ny, sc, sr, cone(30, 10, 8, 100));
        std::vector<ContourLine> lines = trace_contours(f, 60);                  // radius 5 columns
        CHECK(lines.size() == 1 && lines[0].closed);
        double worst = 0;
        for (const CellPoint& p : lines[0].points) worst = std::max(worst, std::fabs(std::hypot(p.col - 30, (p.row - 10) * 2.0) - 5.0));
        CHECK(worst < 0.06);
        CHECK(lines[0].points.size() > 60);
        for (size_t i = 1; i < lines[0].points.size(); i++)                      // followed in order: no jumps
            CHECK(std::hypot(lines[0].points[i].col - lines[0].points[i - 1].col, lines[0].points[i].row - lines[0].points[i - 1].row) < 1.3 * sc);
        // a level above the top, and one below the bottom: nothing
        CHECK(trace_contours(f, 101).empty());
        CHECK(trace_contours(f, -1000).empty());
        // two cones: two loops; one inside a ring: a loop on each side of the ring
        RainField two = field_of(nx, ny, sc, sr, [&](int i, int j) { return std::max(cone(15, 10, 8, 100)(i, j), cone(45, 10, 8, 100)(i, j)); });
        CHECK(trace_contours(two, 60).size() == 2);
        RainField ring = field_of(nx, ny, sc, sr, [&](int i, int j) {
            const double r = std::hypot(i * sc - 30, (j * sr - 10) * 2.0);
            return 100.0 - 20.0 * std::fabs(r - 8.0);                              // highest on a ring of radius 8
        });
        const auto rings = trace_contours(ring, 60);                               // 5 and 11 columns away from the middle: the ring's two edges
        CHECK(rings.size() == 2 && rings[0].closed && rings[1].closed);
        // a level exactly equal to the strengths counts as inside, and a flat field has no lines
        RainField flat = field_of(nx, ny, sc, sr, [](int, int) { return 40.0; });
        CHECK(trace_contours(flat, 40).empty() && trace_contours(flat, 41).empty());
        // a bump touching the edge is a line that ends there, not a loop
        RainField edge = field_of(nx, ny, sc, sr, cone(0, 10, 8, 100));
        const auto cut = trace_contours(edge, 60);
        CHECK(cut.size() == 1 && !cut[0].closed);
        CHECK(std::fabs(cut[0].points.front().col) < 1e-9 && std::fabs(cut[0].points.back().col) < 1e-9);
        // nothing from an empty field, or one too small for a cell
        CHECK(trace_contours(RainField(), 10).empty());
        CHECK(trace_contours(field_of(1, 5, 1, 1, [](int, int) { return 50.0; }), 10).empty());
    }

    // ---- every way a lattice cell can be cut ----
    {
        // a 2 x 2 lattice, one cell: corners a (top left), b (top right), c (bottom right), d (bottom left)
        const double hi = 100, lo = 0;
        for (int mask = 0; mask < 16; mask++) {
            const double a = (mask & 1) ? hi : lo, b = (mask & 2) ? hi : lo, c = (mask & 4) ? hi : lo, d = (mask & 8) ? hi : lo;
            RainField f = field_of(2, 2, 10, 10, [&](int i, int j) { return j == 0 ? (i == 0 ? a : b) : (i == 0 ? d : c); });
            const auto lines = trace_contours(f, 50);
            const int cut_edges = ((mask & 1) != ((mask >> 1) & 1)) + (((mask >> 1) & 1) != ((mask >> 2) & 1)) +
                                  (((mask >> 2) & 1) != ((mask >> 3) & 1)) + (((mask >> 3) & 1) != (mask & 1));
            size_t points = 0;
            for (const auto& l : lines) {
                points += l.points.size();
                CHECK(!l.closed);                                                  // a single cell: every line runs off its edge
                for (const CellPoint& p : l.points) {                              // on the middle of an edge (the level is half way)
                    const bool on_vertical = (std::fabs(p.col) < 1e-9 || std::fabs(p.col - 10) < 1e-9) && std::fabs(p.row - 5) < 1e-9;
                    const bool on_horizontal = (std::fabs(p.row) < 1e-9 || std::fabs(p.row - 10) < 1e-9) && std::fabs(p.col - 5) < 1e-9;
                    CHECK(on_vertical || on_horizontal);
                }
            }
            CHECK((int)points == cut_edges);                                       // one point on each edge that has the level across it
            CHECK(lines.size() == (cut_edges == 4 ? 2u : (cut_edges == 2 ? 1u : 0u)));
        }
        // the saddles: a and c high, b and d low. With the middle high (mean 50 or more) the low corners are cut off one by one,
        // with the middle low the high corners are
        for (int centre_high = 0; centre_high < 2; centre_high++) {
            const double level = centre_high ? 40 : 60;                            // the mean of the corners is 50
            RainField f = field_of(2, 2, 10, 10, [&](int i, int j) { return (i == j) ? 100.0 : 0.0; });
            const auto lines = trace_contours(f, level);
            CHECK(lines.size() == 2);
            auto joined = [&](CellPoint p, CellPoint q) {                           // are the two crossings on one line?
                for (const auto& l : lines) {
                    bool has_p = false, has_q = false;
                    for (const CellPoint& x : l.points) { has_p |= std::hypot(x.col - p.col, x.row - p.row) < 1e-6; has_q |= std::hypot(x.col - q.col, x.row - q.row) < 1e-6; }
                    if (has_p && has_q) return true;
                }
                return false;
            };
            // crossings along an edge between 100 and 0 at the level: at (100 - level) / 100 of the way from the high end
            const double t = (100.0 - level) / 100.0 * 10;                         // from the high corner
            const CellPoint top_edge{t, 0};                                        // between a (high, left) and b (low, right): from a
            const CellPoint right_edge{10, 10 - t};                                // between b (low, top) and c (high, bottom): from c
            const CellPoint bottom_edge{10 - t, 10};                               // between d (low, left) and c (high, right): from c
            const CellPoint left_edge{0, t};                                       // between a (high, top) and d (low, bottom): from a
            if (centre_high) CHECK(joined(top_edge, right_edge) && joined(left_edge, bottom_edge));
            else CHECK(joined(left_edge, top_edge) && joined(right_edge, bottom_edge));
        }
    }

    // ---- random fields: every point is where the level is, the lines are followed in order, and closed ones are closed ----
    {
        std::mt19937 rng(21);
        long points = 0, off_level = 0, jumps = 0, shared = 0;
        for (int trial = 0; trial < 150; trial++) {
            const int nx = 3 + (int)(rng() % 25), ny = 3 + (int)(rng() % 25);
            RainField f = field_of(nx, ny, 1.0, 1.0, [&](int, int) { return (double)(rng() % 70); });
            if (trial % 3 == 0) smooth_field(f);
            const double level = 10 + (double)(rng() % 50) + 0.5;
            const auto lines = trace_contours(f, level);
            std::vector<CellPoint> all;
            for (const ContourLine& line : lines) {
                for (const CellPoint& p : line.points) {
                    points++;
                    all.push_back(p);
                    // on a lattice edge (one coordinate whole), and the field there is the level, by linear interpolation
                    const bool on_row = std::fabs(p.row - std::round(p.row)) < 1e-9, on_col = std::fabs(p.col - std::round(p.col)) < 1e-9;
                    if (on_row == on_col && !(on_row && on_col)) { off_level++; continue; }
                    double v;
                    if (on_row && on_col) {                                        // a node: the field there is the level
                        v = f.at((int)std::round(p.col), (int)std::round(p.row));
                    } else if (on_row) {
                        const int j = (int)std::round(p.row), i = (int)std::floor(p.col);
                        const double t = p.col - i;
                        v = f.at(i, j) * (1 - t) + f.at(std::min(nx - 1, i + 1), j) * t;
                    } else {
                        const int i = (int)std::round(p.col), j = (int)std::floor(p.row);
                        const double t = p.row - j;
                        v = f.at(i, j) * (1 - t) + f.at(i, std::min(ny - 1, j + 1)) * t;
                    }
                    if (std::fabs(v - level) > 1e-3) off_level++;
                }
                for (size_t i = 1; i < line.points.size(); i++)
                    if (std::hypot(line.points[i].col - line.points[i - 1].col, line.points[i].row - line.points[i - 1].row) > std::sqrt(2.0) + 1e-9) jumps++;
                if (line.closed) {
                    CHECK(line.points.size() >= 3);
                    const CellPoint& a = line.points.front();
                    const CellPoint& b = line.points.back();
                    if (std::hypot(a.col - b.col, a.row - b.row) > std::sqrt(2.0) + 1e-9) jumps++;       // the last joins back to the first
                } else {
                    for (const CellPoint& end : {line.points.front(), line.points.back()}) {              // an open line ends on the edge of the lattice
                        const bool edge = end.col < 1e-9 || end.row < 1e-9 || std::fabs(end.col - (nx - 1)) < 1e-9 || std::fabs(end.row - (ny - 1)) < 1e-9;
                        if (!edge) jumps++;
                    }
                }
            }
            for (size_t a = 0; a < all.size(); a++)                                 // no crossing is on two lines
                for (size_t b = a + 1; b < all.size(); b++)
                    if (std::fabs(all[a].col - all[b].col) < 1e-9 && std::fabs(all[a].row - all[b].row) < 1e-9) shared++;
        }
        CHECK(points > 3000);
        CHECK(off_level == 0);
        CHECK(jumps == 0);
        CHECK(shared == 0);
    }

    // ---- smoothing the field ----
    {
        RainField flat = field_of(8, 6, 0.5, 0.25, [](int, int) { return 33.0; });
        smooth_field(flat);
        for (float v : flat.value) CHECK_NEAR(v, 33.0, 1e-4);                      // a flat field stays flat, edges included
        RainField spike = field_of(9, 9, 1, 1, [](int i, int j) { return (i == 4 && j == 4) ? 90.0 : 0.0; });
        smooth_field(spike);
        CHECK_NEAR(spike.at(4, 4), 10.0, 1e-4);                                    // 90 / 9
        CHECK_NEAR(spike.at(3, 3), 10.0, 1e-4);
        CHECK_NEAR(spike.at(5, 4), 10.0, 1e-4);
        CHECK_NEAR(spike.at(2, 4), 0.0, 1e-4);                                     // two away: untouched
        double sum = 0;
        for (float v : spike.value) sum += v;
        CHECK_NEAR(sum, 90.0, 1e-3);                                               // away from the edge nothing is lost
        RainField corner = field_of(5, 5, 1, 1, [](int i, int j) { return (i == 0 && j == 0) ? 90.0 : 0.0; });
        smooth_field(corner);
        CHECK(corner.at(0, 0) > 10.0 && corner.at(0, 0) < 50.0);                   // the edge counts its own value for what is outside
        RainField empty;
        smooth_field(empty);                                                       // no crash
        CHECK(empty.value.empty());
    }

    // ---- laying the radar over the screen ----
    {
        const View view = make_view(60, 20, 40, -100, 300);
        const GeoBox box = view.box(0.2);
        // no picture, or no screen: an empty field
        CHECK(sample_rain_field(RadarFrame(), view).nx == 0);
        // the lattice: nodes from the first corner to the last, a half column and a quarter row apart
        const RadarFrame uniform = make_frame(box, 80, 60, [](int, int) { return 30; });
        const RainField f = sample_rain_field(uniform, view);
        CHECK(f.nx == 121 && f.ny == 81);
        CHECK_NEAR(f.step_col, 0.5, 1e-12);
        CHECK_NEAR(f.step_row, 0.25, 1e-12);
        CHECK(f.value.size() == (size_t)121 * 81);
        for (float v : f.value) CHECK_NEAR(v, 30.0, 1e-3);                         // everywhere the picture is, the strength is
        // outside the picture: nothing
        const GeoBox half{box.south, box.west, box.north, (box.west + box.east) / 2};
        const RadarFrame left = make_frame(half, 40, 60, [](int, int) { return 30; });
        const RainField l = sample_rain_field(left, view);
        CHECK(l.at(0, 40) > 29 && l.at(120, 40) == 0);
        // a single strong pixel: with peaks every node it touches holds all of it; smoothed between pixel centres it is lower
        const View close = make_view(60, 20, 40, -100, 20);                         // 20 km across: finer than the picture's pixels
        const GeoBox close_box = close.box(0.2);
        const RadarFrame one = make_frame(close_box, 24, 12, [](int x, int y) { return (x == 12 && y == 6) ? 45 : 0; });
        const RainField peak = sample_rain_field(one, close, true), smooth = sample_rain_field(one, close, false);
        float peak_max = 0, smooth_max = 0;
        for (float v : peak.value) peak_max = std::max(peak_max, v);
        for (float v : smooth.value) smooth_max = std::max(smooth_max, v);
        CHECK_NEAR(peak_max, 45, 1e-3);
        CHECK(smooth_max > 20 && smooth_max < peak_max);
        int peak_nodes = 0;
        for (float v : peak.value) peak_nodes += v >= 45;
        CHECK(peak_nodes > 4);                                                     // a pixel is bigger than a lattice step here
        // a view of thousands of km: a node covers many pixels, and takes the strongest; both ways agree
        const View wide = make_view(60, 20, 40, -100, 3000);
        const GeoBox wide_box = wide.box(0.1);
        const RadarFrame dots = make_frame(wide_box, 600, 400, [](int x, int y) { return (x % 50 == 7 && y % 40 == 9) ? 52 : 0; });
        const RainField a = sample_rain_field(dots, wide, true), b = sample_rain_field(dots, wide, false);
        CHECK(a.value == b.value);
        float max_wide = 0;
        for (float v : a.value) max_wide = std::max(max_wide, v);
        CHECK_NEAR(max_wide, 52, 1e-3);                                            // a core is never averaged away
    }

    // ---- rounding and tidying lines ----
    {
        ContourLine square;
        square.closed = true;
        square.points = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        const ContourLine round = round_contour(square);
        CHECK(round.closed && round.points.size() == 8);
        CHECK(area_of(round.points, 1.0) < area_of(square.points, 1.0) && area_of(round.points, 1.0) > 12);
        for (const CellPoint& p : round.points) CHECK(distance_to_line(p, {{0, 0}, {4, 0}, {4, 4}, {0, 4}, {0, 0}}) < 1e-9);    // on the old line
        ContourLine open;
        open.points = {{0, 0}, {3, 0}, {3, 3}, {6, 3}};
        const ContourLine rounded_open = round_contour(open);
        CHECK(!rounded_open.closed && rounded_open.points.front().col == 0 && rounded_open.points.back().col == 6);
        CHECK(rounded_open.points.size() == 2 + 2 * 3 - 2);                         // the ends stay; the 3 segments' inner cut points: 4 + ends
        ContourLine pair;
        pair.points = {{0, 0}, {1, 1}};
        CHECK(round_contour(pair).points.size() == 2);                              // too short to round
        CHECK(round_contour(ContourLine()).points.empty());

        // tidying: a straight run of points loses all but its ends, a bend stays
        ContourLine straight;
        for (int i = 0; i <= 10; i++) straight.points.push_back({(double)i, 2.0});
        auto tidy = tidy_contours({straight}, 2.0, 0.1, 1.0);
        CHECK(tidy.size() == 1 && tidy[0].points.size() == 2);
        ContourLine bent;
        for (int i = 0; i <= 5; i++) bent.points.push_back({(double)i, 0.0});
        for (int i = 1; i <= 5; i++) bent.points.push_back({5.0, (double)i});
        tidy = tidy_contours({bent}, 1.0, 0.1, 1.0);
        CHECK(tidy.size() == 1 && tidy[0].points.size() == 3);
        // no point moves further than the tolerance
        ContourLine wavy;
        for (int i = 0; i <= 40; i++) wavy.points.push_back({i * 0.25, 0.04 * std::sin(i * 0.9)});
        tidy = tidy_contours({wavy}, 1.0, 0.1, 1.0);
        CHECK(tidy.size() == 1 && tidy[0].points.size() < wavy.points.size());
        for (const CellPoint& p : wavy.points) CHECK(distance_to_line(p, tidy[0].points) <= SIMPLIFY_TOLERANCE + 1e-9);
        // specks go: a small loop, a short line; with a marker radius the loop becomes a small circle instead
        ContourLine speck;
        speck.closed = true;
        speck.points = {{5, 5}, {5.3, 5}, {5.3, 5.15}, {5, 5.15}};
        CHECK(tidy_contours({speck}, 2.0, 1.5, 1.0).empty());
        tidy = tidy_contours({speck}, 2.0, 1.5, 1.0, 0.3);
        CHECK(tidy.size() == 1 && tidy[0].closed && tidy[0].points.size() == 8);
        double cx = 0, cy = 0;
        for (const CellPoint& p : tidy[0].points) { cx += p.col / 8; cy += p.row / 8; }
        CHECK_NEAR(cx, 5.15, 1e-9);
        CHECK_NEAR(cy, 5.075, 1e-9);
        for (const CellPoint& p : tidy[0].points) CHECK_NEAR(std::hypot(p.col - cx, (p.row - cy) * 2.0), 0.3, 1e-9);
        ContourLine stub;
        stub.points = {{0, 0}, {0.5, 0}};
        CHECK(tidy_contours({stub}, 2.0, 1.5, 2.0).empty());
        CHECK(tidy_contours({stub}, 2.0, 1.5, 0.2, 0.3).size() == 1);              // a short open line is dropped only if shorter than asked
        CHECK(tidy_contours({stub}, 2.0, 1.5, 2.0, 0.3).empty());                  // (the marker is for loops)
        // a loop stays a loop and the closing point is not repeated
        ContourLine circle;
        circle.closed = true;
        for (int k = 0; k < 40; k++) circle.points.push_back({10 + 4 * std::cos(k * 0.157079), 10 + 2 * std::sin(k * 0.157079)});
        tidy = tidy_contours({circle}, 2.0, 1.5, 2.0);
        CHECK(tidy.size() == 1 && tidy[0].closed && tidy[0].points.size() >= 6 && tidy[0].points.size() < 40);
        CHECK(std::hypot(tidy[0].points.front().col - tidy[0].points.back().col, tidy[0].points.front().row - tidy[0].points.back().row) > 0.05);
        CHECK(tidy_contours({}, 2.0, 1.5, 2.0).empty());
    }

    // ---- bands nested like a topo map ----
    {
        const View view = make_view(100, 30, 40, -100, 300);
        const GeoBox box = view.box(0.1);
        // concentric discs of the four strengths, around the middle of the picture
        const RadarFrame frame = make_frame(box, 400, 300, [](int x, int y) {
            const double col = x / 400.0 * 120 - 10, units = y / 300.0 * 72 - 6;                  // on the screen, in columns and in column-sized units
            const double r = std::hypot(col - 50, units - 30);
            if (r < 4) return 55;
            if (r < 8) return 45;
            if (r < 12) return 35;
            if (r < 16) return 20;
            return 0;
        });
        const RainContours contours = make_rain_contours(frame, view);
        CHECK(!contours.empty());
        for (int k = 0; k < RAIN_BANDS; k++) CHECK(contours.band[k].size() == 1 && contours.band[k][0].closed);
        // each band's line encloses the next one's
        for (int k = 0; k + 1 < RAIN_BANDS; k++)
            for (const CellPoint& p : contours.band[k + 1][0].points) CHECK(inside_polygon(p, contours.band[k][0].points));
        // all inside the screen
        for (int k = 0; k < RAIN_BANDS; k++)
            for (const CellPoint& p : contours.band[k][0].points) CHECK(p.col >= 0 && p.col <= 100 && p.row >= 0 && p.row <= 30);
        // the areas fall from light to severe
        for (int k = 0; k + 1 < RAIN_BANDS; k++) CHECK(area_of(contours.band[k][0].points, 2.0) > area_of(contours.band[k + 1][0].points, 2.0));
        // no rain: nothing; rain only light: only the light band
        CHECK(make_rain_contours(make_frame(box, 100, 100, [](int, int) { return 0; }), view).empty());
        const RadarFrame light_only = make_frame(box, 400, 300, [](int x, int y) { return std::hypot(x - 200, y - 150) < 80 ? 22 : 0; });
        const RainContours l = make_rain_contours(light_only, view);
        CHECK(l.band[0].size() == 1 && l.band[1].empty() && l.band[2].empty() && l.band[3].empty());
        // a single strong pixel in a quiet picture is not lost: it is a small circle in the band it reaches
        const RadarFrame core = make_frame(box, 400, 300, [](int x, int y) { return (x == 200 && y == 150) ? 52 : 0; });
        const RainContours c = make_rain_contours(core, view);
        CHECK(c.band[3].size() >= 1 && c.band[3][0].closed);
        // a view with no picture
        CHECK(make_rain_contours(RadarFrame(), view).empty());
    }

    // ---- how a band looks ----
    {
        // in colour: solid and coloured, each different; without colour: white, in four styles
        for (int k = 0; k < RAIN_BANDS; k++) {
            const RainStyle colour = rain_style(k, false), white = rain_style(k, true);
            CHECK(colour.line == LineStyle::Solid);
            CHECK(white.r == 255 && white.g == 255 && white.b == 255);
            for (int j = k + 1; j < RAIN_BANDS; j++) {
                const RainStyle other = rain_style(j, false);
                CHECK(!(colour.r == other.r && colour.g == other.g && colour.b == other.b));
                CHECK(white.line != rain_style(j, true).line);
            }
        }
        CHECK(rain_style(0, true).line == LineStyle::Dotted && rain_style(1, true).line == LineStyle::Dashed);
        CHECK(rain_style(2, true).line == LineStyle::Solid && rain_style(3, true).line == LineStyle::Doubled);
        CHECK(rain_style(-3, true).line == LineStyle::Dotted && rain_style(99, true).line == LineStyle::Doubled);     // out of range: the nearest band
        // the colours go from green through yellow and orange to magenta
        CHECK(rain_style(0, false).g > rain_style(0, false).r && rain_style(1, false).r > 200 && rain_style(1, false).g > 200);
        CHECK(rain_style(2, false).r > 200 && rain_style(2, false).g < 150 && rain_style(3, false).b > 150 && rain_style(3, false).r > 150);
        // tints: a dim version of the same colour, so that text and the map's lines show over them
        for (int k = 0; k < RAIN_BANDS; k++) {
            const RainTint t = rain_tint(k);
            const RainStyle s = rain_style(k, false);
            CHECK(t.r <= s.r * 0.4 && t.g <= s.g * 0.4 && t.b <= s.b * 0.4);
            CHECK(t.r + t.g + t.b > 20);
            CHECK(std::max({t.r, t.g, t.b}) < 90);
        }
        CHECK(rain_tint(-1).g == rain_tint(0).g && rain_tint(9).b == rain_tint(3).b);
    }

    // ---- dashes ----
    {
        // a straight line of length 100: dashes of 10 with gaps of 5 start at 0, 15, 30, ...
        const auto pieces = dash_line({{0, 0}, {100, 0}}, 10, 5);
        CHECK(pieces.size() == 7);                                                  // 0-10, 15-25, 30-40, 45-55, 60-70, 75-85, 90-100
        for (size_t k = 0; k < pieces.size(); k++) {
            CHECK(pieces[k].size() == 2);
            CHECK_NEAR(pieces[k][0].col, 15.0 * k, 1e-9);
            CHECK_NEAR(pieces[k][1].col, 15.0 * k + 10, 1e-9);
            CHECK(pieces[k][0].row == 0 && pieces[k][1].row == 0);
        }
        // a line shorter than a dash is one piece; the end cuts the last dash short
        auto one = dash_line({{0, 0}, {4, 0}}, 10, 5);
        CHECK(one.size() == 1 && one[0].size() == 2 && one[0][1].col == 4);
        auto last = dash_line({{0, 0}, {18, 0}}, 10, 5);
        CHECK(last.size() == 2 && std::fabs(last[1][1].col - 18) < 1e-9 && std::fabs(last[1][0].col - 15) < 1e-9);
        // a dash that goes round a bend keeps the bend
        auto bend = dash_line({{0, 0}, {6, 0}, {6, 10}}, 10, 5);
        CHECK(bend.size() == 2 && bend[0].size() == 3 && std::fabs(bend[0][1].col - 6) < 1e-9 && std::fabs(bend[0][2].row - 4) < 1e-9);
        CHECK(std::fabs(bend[1][0].row - 9) < 1e-9 && std::fabs(bend[1][1].row - 10) < 1e-9);
        // no gap: the line itself. Nothing to dash: nothing.
        CHECK(dash_line({{0, 0}, {50, 0}}, 10, 0).size() == 1);
        CHECK(dash_line({{0, 0}}, 10, 5).empty() && dash_line({}, 10, 5).empty());
        CHECK(dash_line({{0, 0}, {5, 0}}, 0, 5).empty() && dash_line({{0, 0}, {5, 0}}, -1, 5).empty());
        CHECK(dash_line({{0, 0}, {5, 0}}, 10, -1).empty());
        CHECK(dash_line({{3, 3}, {3, 3}}, 10, 5).size() <= 1);                      // no length: no crash
        // random lines: the pieces lie on the line, in order, each dash `on` long except the last, and the gaps are `off`
        std::mt19937 rng(8);
        long bad = 0, checked = 0;
        for (int trial = 0; trial < 200; trial++) {
            std::vector<CellPoint> line;
            double x = 0, y = 0;
            const int n = 2 + (int)(rng() % 6);
            for (int i = 0; i < n; i++) { line.push_back({x, y}); x += (double)(rng() % 40) - 8; y += (double)(rng() % 40) - 15; }
            const double on = 3 + (double)(rng() % 10), off = 1 + (double)(rng() % 9);
            const auto d = dash_line(line, on, off);
            const double total = length_of(line);
            double covered = 0;
            for (const auto& piece : d) {
                if (piece.size() < 2) bad++;
                for (const CellPoint& p : piece) { checked++; if (distance_to_line(p, line) > 1e-6) bad++; }
                covered += length_of(piece);
            }
            // along the line the dashes cover on / (on + off) of it, give or take the end
            const double whole_periods = std::floor(total / (on + off));
            if (covered < whole_periods * on - 1e-6 || covered > whole_periods * on + on + 1e-6) bad++;
            if (!d.empty() && std::hypot(d[0][0].col - line[0].col, d[0][0].row - line[0].row) > 1e-9) bad++;    // starts with a dash at the start
        }
        CHECK(checked > 1000);
        CHECK(bad == 0);
    }

    // ---- lines beside lines ----
    {
        // a line going right: the left side is up the screen
        const auto up = offset_line({{0, 0}, {10, 0}}, 2);
        CHECK(up.size() == 2 && std::fabs(up[0].row + 2) < 1e-9 && std::fabs(up[1].row + 2) < 1e-9 && up[0].col == 0 && up[1].col == 10);
        const auto down = offset_line({{0, 0}, {10, 0}}, -2);
        CHECK(std::fabs(down[0].row - 2) < 1e-9);
        // going down: the left is to the right of the screen
        const auto right = offset_line({{0, 0}, {0, 10}}, 3);
        CHECK(std::fabs(right[0].col - 3) < 1e-9 && std::fabs(right[1].col - 3) < 1e-9);
        // a right-angle bend: the corner point goes out by 1.414 times the distance (a mitre), no further than twice
        const auto corner = offset_line({{0, 0}, {10, 0}, {10, 10}}, 2);
        CHECK(corner.size() == 3);
        CHECK_NEAR(std::hypot(corner[1].col - 10, corner[1].row - 0), 2 * std::sqrt(2.0), 1e-9);
        // a sharp turn back: the mitre is held to twice the distance
        const auto sharp = offset_line({{0, 0}, {10, 0}, {0, 0.5}}, 2);
        CHECK(std::hypot(sharp[1].col - 10, sharp[1].row) <= 2 * 2 + 1e-9);
        // a smooth curve: both sides are the distance away from it, to within a bit
        std::vector<CellPoint> curve;
        for (int i = 0; i <= 60; i++) curve.push_back({i * 1.0, 8 * std::sin(i * 0.1)});
        for (double d : {2.0, -2.0}) {
            const auto side = offset_line(curve, d);
            CHECK(side.size() == curve.size());
            for (const CellPoint& p : side) {
                const double dist = distance_to_line(p, curve);
                CHECK(dist > 1.7 && dist < 2.1);
            }
        }
        // too short to have a side, and no crash with repeated points
        CHECK(offset_line({{1, 1}}, 2).size() == 1 && offset_line({}, 2).empty());
        const auto same = offset_line({{1, 1}, {1, 1}, {1, 1}}, 2);
        for (const CellPoint& p : same) CHECK(std::isfinite(p.col) && std::isfinite(p.row));
    }

    // ---- drawing the bands ----
    {
        const int cols = 60, rows = 20, cw = 13, ch = 26;
        // a made-up set of lines: four straight lines across, one for each band, in different rows
        RainContours rc;
        for (int k = 0; k < RAIN_BANDS; k++) {
            ContourLine line;
            line.points = {{4, 3.0 + 4 * k + 0.5 / 26}, {56, 3.0 + 4 * k + 0.5 / 26}};     // in the middle of a pixel row
            rc.band[k].push_back(line);
        }
        const Blocked none((size_t)cols * rows, 0);
        // in colour: each band's own colour, solid
        {
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, rc, cols, rows, cw, ch, none, false);
            const vtpen::Canvas c = render(d, cols * cw, rows * ch);
            for (int k = 0; k < RAIN_BANDS; k++) {
                const RainStyle s = rain_style(k, false);
                const int n = count_colour(c, {s.r, s.g, s.b});
                CHECK(n > 600 && n < 760);                                          // 52 columns of 13 pixels
            }
        }
        // without colour: all white; dotted has the least ink, then dashed, then solid; doubled has two rows
        {
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, rc, cols, rows, cw, ch, none, true);
            const vtpen::Canvas c = render(d, cols * cw, rows * ch);
            CHECK(count_colour(c, {255, 255, 255}) == count_ink(c));
            auto ink_near_row = [&](double row) {
                int n = 0;
                for (int y = (int)(row * ch) - 4; y <= (int)(row * ch) + 4; y++)
                    for (int x = 0; x < c.width(); x++) n += c.pixel(x, y) != c.background();
                return n;
            };
            const int dotted = ink_near_row(3), dashed = ink_near_row(7), solid = ink_near_row(11), doubled = ink_near_row(15);
            CHECK(dotted > 30 && dotted < dashed && dashed < solid);
            CHECK(solid > 600 && doubled > 1.7 * solid);
            // the double line is two lines a few pixels apart: ink on two rows, a gap between
            std::vector<int> ink_rows;
            for (int y = 15 * ch - 4; y <= 15 * ch + 4; y++) {
                int n = 0;
                for (int x = 0; x < c.width(); x++) n += c.pixel(x, y) != c.background();
                if (n > 400) ink_rows.push_back(y);
            }
            CHECK(ink_rows.size() == 2 && ink_rows.back() - ink_rows.front() >= 2);       // a row with no ink between the two lines
        }
        // a loop is drawn closed
        {
            RainContours loop;
            ContourLine l;
            l.closed = true;
            l.points = {{10, 5}, {20, 5}, {20, 10}, {10, 10}};
            loop.band[1].push_back(l);
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, loop, cols, rows, cw, ch, none, false);
            const vtpen::Canvas c = render(d, cols * cw, rows * ch);
            const RainStyle s = rain_style(1, false);
            int left_side = 0;
            for (int y = 5 * ch; y < 10 * ch; y++) for (int x = 10 * cw - 1; x <= 10 * cw + 1; x++) left_side += c.pixel(x, y) == vtpen::Rgb{s.r, s.g, s.b};
            CHECK(left_side > 100);                                                  // the side from the last point back to the first
        }
        // text in the way: nothing is drawn in the blocked cells, and the rest is still there
        {
            Blocked blocked((size_t)cols * rows, 0);
            for (int r = 0; r < rows; r++) for (int col = 20; col < 30; col++) blocked[(size_t)r * cols + col] = 1;
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, rc, cols, rows, cw, ch, blocked, false);
            const vtpen::Canvas c = render(d, cols * cw, rows * ch);
            int in_blocked = 0;
            for (int y = 0; y < c.height(); y++) for (int x = 20 * cw + 1; x < 30 * cw - 1; x++) in_blocked += c.pixel(x, y) != c.background();
            CHECK(in_blocked == 0);
            CHECK(count_ink(c) > 1500);
        }
        // the heavier band is drawn on top of the lighter where they cross
        {
            RainContours cross;
            ContourLine a, b;
            a.points = {{5, 5}, {25, 5}};
            b.points = {{5, 5}, {25, 5}};
            cross.band[0].push_back(a);
            cross.band[3].push_back(b);
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, cross, cols, rows, cw, ch, none, false);
            const vtpen::Canvas c = render(d, cols * cw, rows * ch);
            const RainStyle severe = rain_style(3, false);
            bool on_top = false;
            for (int y = 5 * ch - 2; y <= 5 * ch + 2; y++) on_top |= c.pixel(10 * cw, y) == vtpen::Rgb{severe.r, severe.g, severe.b};
            CHECK(on_top);
        }
        // nothing to draw: nothing drawn
        {
            vtpen::Drawing d(cols * cw, rows * ch);
            draw_rain_lines(d, RainContours(), cols, rows, cw, ch, none, false);
            CHECK(d.empty());
        }
    }

    // ---- the tints under the rain (colour only) ----
    {
        const View view = make_view(40, 15, 40, -100, 200);
        const CellGrid grid(view);
        const GeoBox box = view.box(0.0);
        // the left half is light rain, the right half severe, the middle column none
        const RadarFrame frame = make_frame(box, 200, 150, [](int x, int) { return x < 90 ? 20 : (x > 110 ? 55 : 0); });
        vtscreen::Picture picture;
        picture.init(15, 40);
        vtscreen::PicCell water;
        water.lum = 0.07; water.shimmer = 0.001; water.alt_lum = 1.0;               // a water cell's letters
        picture.cell[(size_t)3 * picture.cols + 2] = water;
        vtscreen::PicCell text;
        text.text = true; text.ch = U'x'; text.lum = 0.05;
        picture.cell[(size_t)4 * picture.cols + 2] = text;
        vtscreen::PicCell grid_line;
        grid_line.lum = 0.18;
        picture.cell[(size_t)5 * picture.cols + 2] = grid_line;
        draw_rain_tints(grid, frame, picture);
        const RainTint light = rain_tint(0), severe = rain_tint(3);
        auto cell = [&](int row, int col) { return picture.cell[(size_t)row * picture.cols + col]; };
        CHECK(cell(7, 5).alt_lum == 1.0 && cell(7, 5).alt_r == light.r && cell(7, 5).alt_g == light.g && cell(7, 5).alt_b == light.b);
        CHECK(cell(7, 35).alt_r == severe.r && cell(7, 35).alt_g == severe.g && cell(7, 35).alt_b == severe.b);
        CHECK(cell(7, 20).alt_lum == 0 && cell(7, 20).alt_r == 0);                  // no rain in the middle: untouched
        CHECK(cell(3, 2).alt_r == light.r && cell(3, 2).lum == 0);                  // the water's faint letter is blanked
        CHECK(cell(4, 2).alt_r == light.r && cell(4, 2).lum == 0.05 && cell(4, 2).text && cell(4, 2).ch == U'x');   // text stays
        CHECK(cell(5, 2).alt_r == light.r && cell(5, 2).lum == 0.18);               // the grid's letter stays
        // an empty picture, and a frame that is not over the screen: nothing changes
        vtscreen::Picture none;
        none.init(15, 40);
        draw_rain_tints(grid, RadarFrame(), none);
        const RadarFrame far = make_frame(GeoBox{10, 10, 11, 11}, 20, 20, [](int, int) { return 55; });
        draw_rain_tints(grid, far, none);
        for (const vtscreen::PicCell& c : none.cell) CHECK(c.alt_lum == 0);
    }

    // ---- the pen map with the rain ----
    {
        BaseMap map;
        std::string problem;
        load_basemap(find_basemap_file(""), map, problem);
        const View view = make_view(100, 30, 47.6, -122.3, 300);
        const GeoBox box = view.box(0.1);
        const RadarFrame frame = make_frame(box, 200, 150, [](int x, int y) { return std::hypot(x - 100, y - 75) < 30 ? 35 : 0; });
        const Blocked none((size_t)100 * 30, 0);
        const auto with = make_pen_map(map, view, 13, 26, none, false, nullptr);
        const RainContours contours = make_rain_contours(frame, view);
        CHECK(!contours.empty());
        const auto both = make_pen_map(map, view, 13, 26, none, false, &contours);
        const vtpen::Canvas c_with = render(*with, 100 * 13, 30 * 26), c_both = render(*both, 100 * 13, 30 * 26);
        const RainStyle moderate = rain_style(1, false);
        CHECK(count_colour(c_with, {moderate.r, moderate.g, moderate.b}) == 0);
        CHECK(count_colour(c_both, {moderate.r, moderate.g, moderate.b}) > 300);
        // the map's own lines are all still there, on top
        const vtpen::Rgb coast{pen_style_coast().r, pen_style_coast().g, pen_style_coast().b};
        CHECK(count_colour(c_both, coast) > 500 && count_colour(c_both, coast) >= count_colour(c_with, coast) - 400);
        // without colour, all white
        const auto white = make_pen_map(map, view, 13, 26, none, true, &contours);
        const vtpen::Canvas c_white = render(*white, 100 * 13, 30 * 26);
        CHECK(count_colour(c_white, {255, 255, 255}) == count_ink(c_white));
        CHECK(count_ink(c_white) > count_ink(render(*make_pen_map(map, view, 13, 26, none, true, nullptr), 100 * 13, 30 * 26)));

        // PenMap: made again for another picture, and the lines kept while only the text changes
        PenMap pen(map);
        vtscreen::Picture picture;
        auto fresh = [&]() { picture.init(view.rows(), view.cols()); };
        fresh();
        pen.update(view, picture, 13, 26, 0.0, true, false, &frame, 1);
        CHECK(pen.made() == 1 && pen.contours_made() == 1 && picture.vectors.size() == 1);
        fresh();
        pen.update(view, picture, 13, 26, 0.01, true, false, &frame, 1);
        CHECK(pen.made() == 1 && pen.contours_made() == 1);                          // nothing changed
        fresh();
        pen.update(view, picture, 13, 26, 0.02, true, false, &frame, 2);             // another picture: at once
        CHECK(pen.made() == 2 && pen.contours_made() == 2);
        vtscreen::PicCell text;
        text.text = true; text.ch = U'x';
        fresh();
        picture.set(3, 3, text);
        pen.update(view, picture, 13, 26, 0.5, true, false, &frame, 2);              // text appeared: the lines are made again, the rain's are not
        CHECK(pen.made() == 3 && pen.contours_made() == 2);
        fresh();
        pen.update(view, picture, 13, 26, 1.0, true, false, nullptr, 0);              // no picture any more
        CHECK(pen.made() == 4);
        fresh();
        pen.update(view, picture, 13, 26, 1.5, true, false, &frame, 2);              // a picture again: the contours are for the same view and picture
        CHECK(pen.made() == 5 && pen.contours_made() == 2);
        fresh();
        pen.update(view, picture, 13, 26, 2.0, true, true, &frame, 2);               // no colour: made again
        CHECK(pen.made() == 6);
        // the rain's lines are made again when the view they were made for changes, in any way, and not otherwise
        {
            PenMap other(map);
            View v = view;
            double t = 10;
            vtscreen::Picture p2;
            int expected = 0;
            auto step = [&](const char* what, bool changes) {
                p2.init(v.rows(), v.cols());
                t += 1;
                other.update(v, p2, 13, 26, t, true, false, &frame, 7);
                if (changes) expected++;
                if (other.contours_made() != expected) { std::printf("FAIL: contours after changing %s: %d, expected %d\n", what, other.contours_made(), expected); failures++; }
            };
            step("the first picture", true);
            step("nothing", false);
            v.set_centre({47.7, -122.3}); step("the centre's latitude", true);
            v.set_centre({47.7, -122.2}); step("the centre's longitude", true);
            v.set_zoom_km(280); step("the zoom", true);
            v.set_cell_aspect(2.5); step("the cell shape", true);
            v.set_heading(30); step("the heading", true);
            v.set_size(90, 30); step("the width", true);
            v.set_size(90, 28); step("the height", true);
            step("nothing again", false);
        }
        const RadarFrame empty;
        fresh();
        pen.update(view, picture, 13, 26, 2.5, true, true, &empty, 3);               // an empty picture counts as none
        CHECK(pen.made() == 7 && pen.contours_made() == 2);
    }

    // ---- the key to the rain ----
    {
        const Card colour = pen_legend_card(false), white = pen_legend_card(true);
        CHECK(colour.title == "rain" && colour.lines.size() == 4 && white.lines.size() == 4);
        CHECK(colour.lines[0].text == "light" && colour.lines[1].text == "moderate" && colour.lines[2].text == "heavy" && colour.lines[3].text == "severe");
        for (int k = 0; k < 4; k++) {
            const RainStyle s = rain_style(k, false);
            CHECK(colour.lines[(size_t)k].key == "───────");
            CHECK(colour.lines[(size_t)k].key_colour.r == s.r && colour.lines[(size_t)k].key_colour.g == s.g && colour.lines[(size_t)k].key_colour.b == s.b);
            CHECK(white.lines[(size_t)k].key_colour.r == 255 && white.lines[(size_t)k].key_colour.g == 255);
        }
        CHECK(white.lines[0].key == "· · · ·" && white.lines[1].key == "- - - -" && white.lines[2].key == "───────" && white.lines[3].key == "═══════");
        CHECK(card_size(white, 100).rows == PEN_LEGEND_ROWS && card_size(colour, 100).rows == PEN_LEGEND_ROWS);
        // drawn in the bottom right corner, and the stack at the top keeps clear of it
        const View view = make_view(100, 30, 40, -100, 300);
        Settings settings;
        for (int mode : {1, 2}) {
            vtscreen::Picture p;
            p.init(30, 100);
            draw_cards(p, view, settings, "", "", true, true, true, nullptr, false, mode);
            const int bottom = p.bar_row() - 1;                                       // the last row above the bar
            bool legend_text = false;
            for (int r = bottom - PEN_LEGEND_ROWS + 1; r <= bottom; r++)
                for (int c = 60; c < 100; c++) legend_text |= p.cell[(size_t)r * p.cols + c].text;
            CHECK(legend_text);
        }
        // the letters' key is as it was when the pen is not drawing the rain
        vtscreen::Picture old_way, again;
        old_way.init(30, 100); again.init(30, 100);
        draw_cards(old_way, view, settings, "", "", true, true, true, nullptr, false, 0);
        draw_cards(again, view, settings, "", "", true, true, true);
        CHECK(old_way.cell == again.cell);
    }

    // ---- a real picture: Seattle's rain ----
    {
        const GeoBox seattle{47.0, -123.4, 48.2, -121.6};
        const RadarFrame frame = load_frame("noaa_seattle_rain", seattle);
        const View view = make_view(100, 30, 47.6, -122.5, 120);
        const RainContours contours = make_rain_contours(frame, view);
        CHECK(!contours.band[0].empty());                                            // widespread light to moderate rain
        CHECK(contours.band[3].empty() && contours.band[2].empty());                 // nothing heavy that day
        for (int k = 0; k < RAIN_BANDS; k++)
            for (const ContourLine& l : contours.band[k])
                for (const CellPoint& p : l.points) CHECK(std::isfinite(p.col) && std::isfinite(p.row) && p.col >= -1e-9 && p.col <= 100 + 1e-9 && p.row >= -1e-9 && p.row <= 30 + 1e-9);
    }

    // ---- random pictures and views: no trouble, nothing outside the screen ----
    {
        std::mt19937 rng(77);
        long lines = 0, bad_points = 0;
        for (int trial = 0; trial < 60; trial++) {
            const int cols = 1 + (int)(rng() % 150), rows = 1 + (int)(rng() % 50);
            View view = make_view(cols, rows, 25 + (double)(rng() % 25), -120 + (double)(rng() % 50), 5 + (double)(rng() % 3000));
            if (trial % 4 == 0) view.set_heading((double)(rng() % 360));
            const GeoBox box = view.box((double)(rng() % 30) / 100.0);
            const int w = 5 + (int)(rng() % 300), h = 5 + (int)(rng() % 300);
            const RadarFrame frame = make_frame(box, w, h, [&](int, int) { return (rng() % 5 == 0) ? (int)(rng() % 70) : 0; });
            const RainContours c = make_rain_contours(frame, view);
            for (int k = 0; k < RAIN_BANDS; k++)
                for (const ContourLine& l : c.band[k]) {
                    lines++;
                    CHECK(l.points.size() >= 2);
                    for (const CellPoint& p : l.points)
                        if (!std::isfinite(p.col) || !std::isfinite(p.row) || p.col < -1e-6 || p.col > cols + 1e-6 || p.row < -1e-6 || p.row > rows + 1e-6) bad_points++;
                }
        }
        CHECK(lines > 100);
        CHECK(bad_points == 0);
    }

    // ---- the lattice between the radar's pixels (checked against a plain calculation) ----
    {
        // where the pixels are bigger than a lattice step, a node reads the picture between pixel centres: a ramp that rises with the
        // pixel's x (or y) number is read back exactly, at the node's own place
        const View close = make_view(60, 20, 40, -100, 20);
        const GeoBox box = close.box(0.2);
        auto node_place = [&](const RainField& f, int i, int j) {                  // the middle of the node's footprint
            double south = 1e9, north = -1e9, west = 1e9, east = -1e9;
            for (int dj = 0; dj < 2; dj++)
                for (int di = 0; di < 2; di++) {
                    const GeoPoint p = close.at((i - 0.5 + di) * f.step_col, (j - 0.5 + dj) * f.step_row);
                    south = std::min(south, p.lat); north = std::max(north, p.lat); west = std::min(west, p.lon); east = std::max(east, p.lon);
                }
            return GeoPoint{(south + north) / 2, (west + east) / 2};
        };
        const int w = 24, h = 12;
        const RadarFrame ramp_x = make_frame(box, w, h, [](int x, int) { return 10 * x; });
        const RadarFrame ramp_y = make_frame(box, w, h, [](int, int y) { return 20 * y; });
        const RainField fx = sample_rain_field(ramp_x, close), fy = sample_rain_field(ramp_y, close);
        const double pw = (box.east - box.west) / w, ph = (box.north - box.south) / h;
        int checked_x = 0, checked_y = 0;
        for (int j = 0; j < fx.ny; j++)
            for (int i = 0; i < fx.nx; i++) {
                const GeoPoint at = node_place(fx, i, j);
                const double px = (at.lon - box.west) / pw - 0.5, py = (box.north - at.lat) / ph - 0.5;
                if (px >= 0 && px <= w - 1) { CHECK_NEAR(fx.at(i, j), 10 * px, 0.01); checked_x++; }
                if (py >= 0 && py <= h - 1) { CHECK_NEAR(fy.at(i, j), 20 * py, 0.02); checked_y++; }
            }
        CHECK(checked_x > 4000 && checked_y > 3000);
        // a picture that covers only part of the screen: a node between its last pixel's middle and its edge fades out to nothing
        // (nothing beyond), and one beyond its first pixel likewise, so an edge never reads another row's pixels
        const GeoBox middle{box.south, box.west + (box.east - box.west) * 0.25, box.north, box.west + (box.east - box.west) * 0.75};
        const RadarFrame part = make_frame(middle, 12, 12, [](int, int) { return 30; });
        const RainField pf = sample_rain_field(part, close);
        const double mw = (middle.east - middle.west) / 12;
        for (int j = 0; j < pf.ny; j++)
            for (int i = 0; i < pf.nx; i++) {
                const GeoPoint at = node_place(pf, i, j);
                const double px = (at.lon - middle.west) / mw - 0.5, py = (middle.north - at.lat) / ((middle.north - middle.south) / 12) - 0.5;
                const double wx = px < -1 || px > 12 ? 0 : (px < 0 ? 1 + px : (px > 11 ? 12 - px : 1));
                const double wy = py < -1 || py > 12 ? 0 : (py < 0 ? 1 + py : (py > 11 ? 12 - py : 1));
                CHECK_NEAR(pf.at(i, j), 30 * wx * wy, 0.02);
            }
        // many pixels under a node, in one direction only: the strongest, as in the other (a thin strong stripe is not smoothed away)
        const RadarFrame stripe_x = make_frame(box, 480, 4, [](int x, int) { return x == 240 ? 45 : 0; });     // fine across, coarse down
        const RadarFrame stripe_y = make_frame(box, 4, 480, [](int, int y) { return y == 240 ? 45 : 0; });     // coarse across, fine down
        for (const RadarFrame* f : {&stripe_x, &stripe_y}) {
            const RainField sf = sample_rain_field(*f, close);
            int full = 0;
            float biggest = 0;
            for (float v : sf.value) { biggest = std::max(biggest, v); full += v == 45; }
            CHECK(biggest == 45 && full >= 2);
        }
        // and where a node covers fewer than one and a half pixels it smooths: one strong pixel does not fill every node near it
        const RadarFrame dot = make_frame(box, 24, 12, [](int x, int y) { return (x == 12 && y == 6) ? 45 : 0; });
        const RainField df = sample_rain_field(dot, close);
        int full = 0;
        for (float v : df.value) full += v >= 44.999f;
        CHECK(full == 0);                                                          // no node is exactly on the pixel's middle
        // frames with a side of nothing are no frame
        RadarFrame no_height = make_frame(box, 5, 5, [](int, int) { return 30; });
        no_height.height = 0;
        RadarFrame no_width = make_frame(box, 5, 5, [](int, int) { return 30; });
        no_width.width = 0;
        CHECK(sample_rain_field(no_height, close).nx == 0 && sample_rain_field(no_width, close).nx == 0);
    }

    // ---- saddles with any four corners ----
    {
        std::mt19937 rng(5);
        int checked = 0;
        for (int trial = 0; trial < 400; trial++) {
            // a (top left) and c (bottom right) at or above the level, b and d below it (or the other way round)
            const double level = 50;
            const bool flip = rng() % 2;
            auto high = [&]() { return level + (double)(rng() % 40); };
            auto low = [&]() { return level - 1 - (double)(rng() % 40); };
            double a = high(), c = high(), b = low(), d = low();
            if (trial % 5 == 0) {                                                  // the middle exactly at the level
                a = 80; c = 60 + (double)(rng() % 20); b = 40 - (double)(rng() % 20); d = 4 * level - a - b - c;
                if (d >= level || d < 0) continue;
            }
            if (flip) { std::swap(a, b); std::swap(c, d); }
            RainField f = field_of(2, 2, 10, 10, [&](int i, int j) { return j == 0 ? (i == 0 ? a : b) : (i == 0 ? d : c); });
            const auto lines = trace_contours(f, level);
            CHECK(lines.size() == 2);
            const bool centre_inside = (a + b + c + d) / 4 >= level;
            // the crossing on each side
            auto cross = [&](double v1, double v2) { return (level - v1) / (v2 - v1); };
            const CellPoint top{10 * cross(a, b), 0}, right{10, 10 * cross(b, c)}, bottom{10 * cross(d, c), 10}, left{0, 10 * cross(a, d)};
            auto joined = [&](CellPoint p, CellPoint q) {
                for (const auto& l : lines) {
                    bool has_p = false, has_q = false;
                    for (const CellPoint& x : l.points) { has_p |= std::hypot(x.col - p.col, x.row - p.row) < 1e-6; has_q |= std::hypot(x.col - q.col, x.row - q.row) < 1e-6; }
                    if (has_p && has_q) return true;
                }
                return false;
            };
            // which corners are above the level decides what the middle joins: if it is inside, the corners above are joined to each other,
            // so each corner below is cut off alone
            const bool a_high = a >= level;                                         // else b and d are the high ones
            bool ok;
            if (a_high) ok = centre_inside ? (joined(top, right) && joined(left, bottom)) : (joined(left, top) && joined(right, bottom));
            else ok = centre_inside ? (joined(left, top) && joined(right, bottom)) : (joined(top, right) && joined(left, bottom));
            CHECK(ok);
            checked++;
        }
        CHECK(checked > 300);
    }

    // ---- limits of the tidying (checked on the number) ----
    {
        // a closed loop of a known area stays at exactly the area asked for and goes below it
        ContourLine square;
        square.closed = true;
        square.points = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};                             // area 4 when a cell is as tall as wide
        CHECK(tidy_contours({square}, 1.0, 4.0, 1.0).size() == 1);
        CHECK(tidy_contours({square}, 1.0, 4.001, 1.0).empty());
        CHECK(tidy_contours({square}, 2.0, 8.0, 1.0).size() == 1 && tidy_contours({square}, 2.0, 8.001, 1.0).empty());   // rows count aspect times
        ContourLine triangle;
        triangle.closed = true;
        triangle.points = {{0, 0}, {6, 0}, {3, 5}};
        const auto tri = tidy_contours({triangle}, 1.0, 1.0, 1.0);
        CHECK(tri.size() == 1 && tri[0].closed && tri[0].points.size() == 3);        // three points are enough for a loop
        // an open line's length counts rows aspect times as long, and a line exactly as long as asked for stays
        ContourLine slant;
        slant.points = {{0, 0}, {0.3, 0.2}};                                           // 0.5 long with aspect 2: hypot(0.3, 0.4)
        CHECK(tidy_contours({slant}, 2.0, 1.0, 0.5 - 1e-9).size() == 1);
        CHECK(tidy_contours({slant}, 2.0, 1.0, 0.5 + 1e-6).empty());
        ContourLine half;
        half.points = {{0, 0}, {0.5, 0}};
        CHECK(tidy_contours({half}, 2.0, 1.0, 0.5).size() == 1);
        // a point exactly the tolerance off its neighbours' line is not kept; one a bit further is
        ContourLine on_tolerance;
        on_tolerance.points = {{0, 0}, {5, SIMPLIFY_TOLERANCE}, {10, 0}};
        CHECK(tidy_contours({on_tolerance}, 1.0, 1.0, 1.0)[0].points.size() == 2);
        ContourLine beyond;
        beyond.points = {{0, 0}, {5, SIMPLIFY_TOLERANCE * 1.01}, {10, 0}};
        CHECK(tidy_contours({beyond}, 1.0, 1.0, 1.0)[0].points.size() == 3);
        // distance is to the line's piece, not to the line carried on: a point far past the end of a short chord stays
        ContourLine hairpin;
        hairpin.points = {{0, 0}, {10, 0}, {1, 0.01}};
        const auto kept = tidy_contours({hairpin}, 1.0, 1.0, 1.0);
        CHECK(kept.size() == 1 && kept[0].points.size() == 3 && kept[0].points[1].col == 10);
        // rounding: a line of three points, open and closed
        ContourLine three;
        three.points = {{0, 0}, {4, 0}, {4, 4}};
        CHECK(round_contour(three).points.size() == 4);
        three.closed = true;
        CHECK(round_contour(three).points.size() == 6);
    }

    // ---- the whole way from a picture to lines (the pieces in order, as the program makes them) ----
    {
        const GeoBox seattle{47.0, -123.4, 48.2, -121.6};
        const RadarFrame real = load_frame("noaa_seattle_rain", seattle);
        const View view = make_view(100, 30, 47.6, -122.5, 120);
        RainField smooth = sample_rain_field(real, view, false);
        const RainField peaks = sample_rain_field(real, view, true);
        smooth_field(smooth);
        const RainContours made = make_rain_contours(real, view);
        for (int k = 0; k < RAIN_BANDS; k++) {
            std::vector<ContourLine> lines = trace_contours(k == 0 ? smooth : peaks, RAIN_BAND_MIN_DBZ[k]);
            for (int pass = 0; pass < (k == 0 ? 1 : 2); pass++)
                for (ContourLine& l : lines) l = round_contour(l);
            const auto expected = tidy_contours(lines, view.cell_aspect(), MIN_LOOP_AREA_BY_BAND[k], MIN_LINE_LENGTH_BY_BAND[k], MARKER_RADIUS_BY_BAND[k]);
            CHECK(made.band[k].size() == expected.size());
            for (size_t n = 0; n < made.band[k].size() && n < expected.size(); n++) {
                CHECK(made.band[k][n].closed == expected[n].closed && made.band[k][n].points.size() == expected[n].points.size());
                for (size_t m = 0; m < made.band[k][n].points.size() && m < expected[n].points.size(); m++)
                    CHECK(made.band[k][n].points[m].col == expected[n].points[m].col && made.band[k][n].points[m].row == expected[n].points[m].row);
            }
        }
        // the same on made-up discs, which reach every band
        const View dview = make_view(100, 30, 40, -100, 300);
        const RadarFrame discs = make_frame(dview.box(0.1), 400, 300, [](int x, int y) {
            const double col = x / 400.0 * 120 - 10, units = y / 300.0 * 72 - 6;
            const double r = std::hypot(col - 50, units - 30);
            return r < 3 ? 55 : (r < 6 ? 45 : (r < 9 ? 35 : (r < 12 ? 20 : 0)));
        });
        RainField dsmooth = sample_rain_field(discs, dview, false);
        const RainField dpeaks = sample_rain_field(discs, dview, true);
        smooth_field(dsmooth);
        const RainContours dmade = make_rain_contours(discs, dview);
        for (int k = 0; k < RAIN_BANDS; k++) {
            std::vector<ContourLine> lines = trace_contours(k == 0 ? dsmooth : dpeaks, RAIN_BAND_MIN_DBZ[k]);
            for (int pass = 0; pass < (k == 0 ? 1 : 2); pass++)
                for (ContourLine& l : lines) l = round_contour(l);
            const auto expected = tidy_contours(lines, dview.cell_aspect(), MIN_LOOP_AREA_BY_BAND[k], MIN_LINE_LENGTH_BY_BAND[k], MARKER_RADIUS_BY_BAND[k]);
            CHECK(dmade.band[k].size() == 1 && expected.size() == 1 && dmade.band[k][0].points.size() == expected[0].points.size());
            for (size_t m = 0; m < dmade.band[k][0].points.size() && m < expected[0].points.size(); m++)
                CHECK(dmade.band[k][0].points[m].col == expected[0].points[m].col && dmade.band[k][0].points[m].row == expected[0].points[m].row);
        }
    }

    // ---- the sizes of dashes, and the bends of lines beside lines ----
    {
        const DashSizes at13 = dash_sizes(13);
        CHECK_NEAR(at13.dot_on, 2, 1e-12);  CHECK_NEAR(at13.dot_off, 6, 1e-12);
        CHECK_NEAR(at13.dash_on, 12, 1e-12);  CHECK_NEAR(at13.dash_off, 6, 1e-12);
        CHECK_NEAR(at13.double_gap, 2.4, 1e-12);
        const DashSizes tiny = dash_sizes(2);                                          // the scale does not go below a half
        CHECK_NEAR(tiny.dot_on, 1, 1e-12);  CHECK_NEAR(tiny.dash_on, 6, 1e-12);  CHECK_NEAR(tiny.double_gap, 2.2, 1e-12);       // and the gap stays two pixels or more
        const DashSizes huge = dash_sizes(200);                                        // or above three
        CHECK_NEAR(huge.dot_on, 6, 1e-12);  CHECK_NEAR(huge.dash_off, 18, 1e-12);  CHECK_NEAR(huge.double_gap, 7.2, 1e-12);
        CHECK(dash_sizes(26).dash_on > at13.dash_on && dash_sizes(26).double_gap > at13.double_gap);

        // dashes meeting the end of the line exactly
        CHECK(dash_line({{0, 0}, {10, 0}}, 10, 5).size() == 1);                        // the dash ends where the line does
        CHECK(dash_line({{0, 0}, {15, 0}}, 10, 5).size() == 1);                        // the gap ends where the line does: no dash of no length
        auto exact = dash_line({{0, 0}, {25, 0}}, 10, 5);
        CHECK(exact.size() == 2 && exact[1].size() == 2 && std::fabs(exact[1][0].col - 15) < 1e-9 && std::fabs(exact[1][1].col - 25) < 1e-9);
        for (double length : {15.0, 30.0, 45.0})
            for (const auto& piece : dash_line({{0, 0}, {length, 0}}, 10, 5)) CHECK(piece.size() >= 2);
        // a line that turns back on itself: the side line is a point out by the whole distance, not a number that is not a number
        const auto back = offset_line({{0, 0}, {10, 0}, {0, 0}}, 2);
        CHECK(back.size() == 3 && std::isfinite(back[1].col) && std::isfinite(back[1].row));
        CHECK_NEAR(back[1].col, 10, 1e-9);  CHECK_NEAR(back[1].row, -2, 1e-9);
        // and a sharp turn goes out exactly twice the distance (the mitre's limit), no more and no less
        const auto sharp = offset_line({{0, 0}, {10, 0}, {0, 0.5}}, 2);
        CHECK_NEAR(std::hypot(sharp[1].col - 10, sharp[1].row), 4, 1e-6);
    }

    // ---- tints stay in the map's rows ----
    {
        const View view = make_view(40, 15, 40, -100, 200);
        const CellGrid grid(view);
        const RadarFrame frame = make_frame(view.box(0.0), 200, 150, [](int, int) { return 55; });
        vtscreen::Picture picture;
        picture.init(16, 40);                                                       // the map's 15 rows and the bar row
        draw_rain_tints(grid, frame, picture);
        for (int col = 0; col < 40; col++) {
            CHECK(picture.cell[(size_t)14 * picture.cols + col].alt_lum == 1.0);
            CHECK(picture.cell[(size_t)15 * picture.cols + col].alt_lum == 0 && picture.cell[(size_t)15 * picture.cols + col].alt_r == 0);   // the bar row is not tinted
        }
    }

    if (failures == 0) std::printf("all rain contour tests passed\n");
    return failures ? 1 : 0;
}
