// Tests for the base map: its file format, the ring simplifier, filling land and water into the
// cells of a view (on made-up maps with known shapes), and a handful of known places on the real
// map file in data/.
#include <algorithm>
#include <chrono>
#include <random>
#include <string>

#include "check.h"

#include "../src/basemap.h"
#include "../src/ground_map.h"

#ifndef VTW_DATA_DIR
#error "VTW_DATA_DIR must name the data folder"
#endif

namespace {

using vtw::BaseMap;
using vtw::GeoBox;
using vtw::GeoPoint;
using vtw::Ground;
using vtw::GroundMap;
using vtw::LayerKind;
using vtw::MapLayer;
using vtw::MapPolygon;

std::vector<int32_t> square(double south, double west, double north, double east) {
    auto u = [](double d) { return (int32_t)std::lround(d * vtw::MAP_UNITS_PER_DEGREE); };
    return {u(west), u(south), u(east), u(south), u(east), u(north), u(west), u(north)};
}

MapPolygon polygon_of(std::vector<std::vector<int32_t>> rings) {
    MapPolygon p;
    p.rings = std::move(rings);
    const std::vector<int32_t>& o = p.rings[0];
    double south = 90, north = -90, west = 180, east = -180;
    for (size_t i = 0; i < o.size(); i += 2) {
        west = std::min(west, o[i] / vtw::MAP_UNITS_PER_DEGREE); east = std::max(east, o[i] / vtw::MAP_UNITS_PER_DEGREE);
        south = std::min(south, o[i + 1] / vtw::MAP_UNITS_PER_DEGREE); north = std::max(north, o[i + 1] / vtw::MAP_UNITS_PER_DEGREE);
    }
    p.box = {south, west, north, east};
    return p;
}

std::vector<int32_t> line_of(std::vector<std::pair<double, double>> lon_lat) {
    std::vector<int32_t> pts;
    for (auto& p : lon_lat) {
        pts.push_back((int32_t)std::lround(p.first * vtw::MAP_UNITS_PER_DEGREE));
        pts.push_back((int32_t)std::lround(p.second * vtw::MAP_UNITS_PER_DEGREE));
    }
    return pts;
}

vtw::MapLine map_line(std::vector<std::pair<double, double>> lon_lat) {
    vtw::MapLine l;
    l.points = line_of(lon_lat);
    double south = 90, north = -90, west = 180, east = -180;
    for (auto& p : lon_lat) {
        west = std::min(west, p.first); east = std::max(east, p.first);
        south = std::min(south, p.second); north = std::max(north, p.second);
    }
    l.box = {south, west, north, east};
    return l;
}

MapLayer lines_of(LayerKind kind, int detail, GeoBox coverage, std::vector<vtw::MapLine> lines) {
    MapLayer l;
    l.kind = kind; l.detail = detail; l.coverage = coverage; l.lines = std::move(lines);
    return l;
}

MapLayer layer_of(LayerKind kind, int detail, GeoBox coverage, std::vector<MapPolygon> polygons) {
    MapLayer l;
    l.kind = kind; l.detail = detail; l.coverage = coverage; l.polygons = std::move(polygons);
    return l;
}

const GeoBox WORLD{-90, -180, 90, 180};

vtw::View make_view(double lat, double lon, double km, int cols = 100, int rows = 40) {
    vtw::View v;
    v.set_cell_aspect(2.0);
    v.set_centre({lat, lon});
    v.set_zoom_km(km);
    v.set_size(cols, rows);
    return v;
}

// ---- the file

void file_tests() {
    BaseMap map;
    map.layers.push_back(layer_of(LayerKind::Land, 0, WORLD, {polygon_of({square(10, 20, 30, 40), square(15, 25, 20, 30)})}));
    map.layers.push_back(layer_of(LayerKind::Lake, 2, GeoBox{5, -170, 75, -50}, {polygon_of({square(-1.5, -2.5, 3.25, 4.75)})}));
    map.layers.push_back(lines_of(LayerKind::River, 1, WORLD, {map_line({{0, 0}, {1.5, 2.25}, {3, 1}}), map_line({{-5, -5}, {-6, -7}})}));
    map.layers.push_back(lines_of(LayerKind::Country, 2, WORLD, {}));
    map.layers.push_back(lines_of(LayerKind::Highway, 2, GeoBox{5, -170, 75, -50}, {map_line({{-100, 40}, {-99, 41}})}));
    map.layers.push_back(lines_of(LayerKind::Road, 2, GeoBox{5, -170, 75, -50}, {}));
    map.places.push_back({-9712345, 3769000, 389965, "Wichita"});
    map.places.push_back({-12233000, 4760000, 737015, "Seattle"});
    map.places.push_back({100, -200, 0, ""});
    const std::string bytes = vtw::write_basemap(map);

    BaseMap back;
    std::string error;
    CHECK(vtw::read_basemap((const uint8_t*)bytes.data(), bytes.size(), back, error));
    CHECK(back.layers.size() == 6);
    CHECK(back.layers[4].kind == LayerKind::Highway && back.layers[4].lines.size() == 1);
    CHECK(back.layers[5].kind == LayerKind::Road && back.layers[5].lines.empty());
    CHECK(back.layers[2].kind == LayerKind::River && back.layers[2].lines.size() == 2);
    CHECK(back.layers[2].lines[0].points == map.layers[2].lines[0].points);
    CHECK_NEAR(back.layers[2].lines[0].box.north, 2.25, 1e-9);
    CHECK(back.layers[3].kind == LayerKind::Country && back.layers[3].lines.empty());
    CHECK(back.places.size() == 3);
    CHECK(back.places[0].name == "Wichita" && back.places[0].lon == -9712345 && back.places[0].lat == 3769000);
    CHECK(back.places[1].population == 737015 && back.places[2].name.empty() && back.places[2].lat == -200);
    CHECK(back.layers[0].kind == LayerKind::Land && back.layers[0].detail == 0);
    CHECK(back.layers[1].kind == LayerKind::Lake && back.layers[1].detail == 2);
    CHECK_NEAR(back.layers[1].coverage.west, -170, 1e-9);
    CHECK(back.layers[0].polygons[0].rings.size() == 2);
    CHECK(back.layers[0].polygons[0].rings[1] == map.layers[0].polygons[0].rings[1]);
    CHECK_NEAR(back.layers[1].polygons[0].box.south, -1.5, 1e-9);
    CHECK(vtw::write_basemap(back) == bytes);                    // and writes the same bytes again

    // every shortened file is refused; a wrong signature too
    for (size_t length = 0; length < bytes.size(); length++) {
        BaseMap m;
        CHECK(!vtw::read_basemap((const uint8_t*)bytes.data(), length, m, error));
        CHECK(m.layers.empty());
    }
    std::string wrong = bytes;
    wrong[0] = 'X';
    CHECK(!vtw::read_basemap((const uint8_t*)wrong.data(), wrong.size(), back, error) && error.find("not a base map") != std::string::npos);

    // damaged files never crash (counts that cannot be true are refused before any memory is used)
    std::mt19937 rng(11);
    int refused = 0;
    for (int round = 0; round < 5000; round++) {
        std::string damaged = bytes;
        for (int k = 0; k < 1 + (int)(rng() % 3); k++) damaged[8 + rng() % (damaged.size() - 8)] = (char)(rng() % 256);
        BaseMap m;
        if (!vtw::read_basemap((const uint8_t*)damaged.data(), damaged.size(), m, error)) refused++;
    }
    CHECK(refused > 500);
    std::string huge = bytes;                                    // a polygon count of four billion
    for (int i = 0; i < 4; i++) huge[8 + 4 + 4 + 16 + i] = (char)0xFF;
    CHECK(!vtw::read_basemap((const uint8_t*)huge.data(), huge.size(), back, error));

    CHECK(!vtw::load_basemap("/no/such/file.bin", back, error) && error.find("cannot open") != std::string::npos);
    CHECK(vtw::find_basemap_file("/no/such/file.bin").empty());
}

void simplify_tests() {
    // a square with extra points along its sides loses them
    std::vector<int32_t> ring;
    auto add = [&](double lon, double lat) {
        ring.push_back((int32_t)std::lround(lon * vtw::MAP_UNITS_PER_DEGREE));
        ring.push_back((int32_t)std::lround(lat * vtw::MAP_UNITS_PER_DEGREE));
    };
    for (int i = 0; i < 10; i++) add(i, 0);
    for (int i = 0; i < 10; i++) add(10, i);
    for (int i = 10; i > 0; i--) add(i, 10);
    for (int i = 10; i > 0; i--) add(0, i);
    const std::vector<int32_t> simple = vtw::simplify_ring(ring, 0.01);
    CHECK(simple.size() == 8);
    // with no tolerance nothing changes; too small a ring is dropped
    CHECK(vtw::simplify_ring(ring, 0) == ring);
    CHECK(vtw::simplify_ring({0, 0, 5, 5}, 0.1).empty());

    // a wavy circle: no removed point is further than the tolerance from the simplified outline
    std::vector<int32_t> wavy;
    for (int i = 0; i < 720; i++) {
        const double a = i * 3.14159265358979 / 360, r = 5 + 0.05 * std::sin(i * 0.7);
        wavy.push_back((int32_t)std::lround(r * std::cos(a) * vtw::MAP_UNITS_PER_DEGREE));
        wavy.push_back((int32_t)std::lround(r * std::sin(a) * vtw::MAP_UNITS_PER_DEGREE));
    }
    const double tolerance = 0.02;
    const std::vector<int32_t> fewer = vtw::simplify_ring(wavy, tolerance);
    CHECK(fewer.size() < wavy.size() / 2 && fewer.size() >= 12);
    const size_t m = fewer.size() / 2;
    double worst = 0;
    for (size_t i = 0; i < wavy.size(); i += 2) {
        double best = 1e18;
        for (size_t k = 0; k < m; k++) {                         // distance to each segment of the result
            const double ax = fewer[2 * k], ay = fewer[2 * k + 1], bx = fewer[2 * ((k + 1) % m)], by = fewer[2 * ((k + 1) % m) + 1];
            const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            double t = l2 == 0 ? 0 : ((wavy[i] - ax) * dx + (wavy[i + 1] - ay) * dy) / l2;
            t = std::min(std::max(t, 0.0), 1.0);
            best = std::min(best, std::hypot(wavy[i] - (ax + t * dx), wavy[i + 1] - (ay + t * dy)));
        }
        worst = std::max(worst, best);
    }
    CHECK(worst <= tolerance * vtw::MAP_UNITS_PER_DEGREE * 1.001);
}

void simplify_line_tests() {
    // a straight line with many points keeps only its two ends
    std::vector<std::pair<double, double>> straight;
    for (int i = 0; i <= 50; i++) straight.push_back({i * 0.1, i * 0.05});
    const std::vector<int32_t> two = vtw::simplify_line(line_of(straight), 0.001);
    CHECK(two.size() == 4);
    CHECK(two[0] == 0 && two[2] == (int32_t)std::lround(5.0 * vtw::MAP_UNITS_PER_DEGREE));
    // a bend is kept
    const std::vector<int32_t> bent = vtw::simplify_line(line_of({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}, {3, 3}}), 0.01);
    CHECK(bent.size() == 6);                                  // the two ends and the corner
    // with no tolerance nothing changes; fewer than two points is nothing
    CHECK(vtw::simplify_line(line_of({{0, 0}, {1, 1}, {2, 0}}), 0) == line_of({{0, 0}, {1, 1}, {2, 0}}));
    CHECK(vtw::simplify_line(line_of({{0, 0}}), 0.1).empty());
    // no point of a wavy line is further from the result than the tolerance, and the ends stay
    std::vector<std::pair<double, double>> wavy;
    for (int i = 0; i <= 400; i++) wavy.push_back({i * 0.02, std::sin(i * 0.05) * 0.5 + 0.01 * std::sin(i * 1.3)});
    const double tolerance = 0.03;
    const std::vector<int32_t> fewer = vtw::simplify_line(line_of(wavy), tolerance);
    const std::vector<int32_t> original = line_of(wavy);
    CHECK(fewer.size() < original.size() / 2 && fewer.size() >= 8);
    CHECK(fewer[0] == original[0] && fewer[fewer.size() - 2] == original[original.size() - 2]);
    double worst = 0;
    for (size_t i = 0; i < original.size(); i += 2) {
        double best = 1e18;
        for (size_t k = 0; k + 3 < fewer.size(); k += 2) {
            const double ax = fewer[k], ay = fewer[k + 1], bx = fewer[k + 2], by = fewer[k + 3];
            const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            double t = l2 == 0 ? 0 : ((original[i] - ax) * dx + (original[i + 1] - ay) * dy) / l2;
            t = std::min(std::max(t, 0.0), 1.0);
            best = std::min(best, std::hypot(original[i] - (ax + t * dx), original[i + 1] - (ay + t * dy)));
        }
        worst = std::max(worst, best);
    }
    CHECK(worst <= tolerance * vtw::MAP_UNITS_PER_DEGREE * 1.001);
}

// ---- filling cells

int count(const GroundMap& g, Ground kind) {
    int n = 0;
    for (int r = 0; r < g.rows(); r++) for (int c = 0; c < g.cols(); c++) n += g.at(c, r) == kind;
    return n;
}

void fill_tests() {
    // a square island 2 degrees wide with a square lake in the middle, in a view that shows it whole
    BaseMap map;
    map.layers.push_back(layer_of(LayerKind::Land, 2, WORLD, {polygon_of({square(-1, -1, 1, 1)})}));
    map.layers.push_back(layer_of(LayerKind::Lake, 2, WORLD, {polygon_of({square(-0.25, -0.25, 0.25, 0.25)})}));
    vtw::View view = make_view(0, 0, 700, 140, 70);              // about 5 km per column: the island is ~44 columns wide
    GroundMap ground(map);
    ground.update(view);
    CHECK(ground.detail() == 2);

    // the middle is the lake, the ring around it is land, far away is water
    CHECK(ground.at(70, 35) == Ground::Water);
    CHECK(ground.at(70 + 12, 35) == Ground::Land || ground.at(70 + 12, 35) == Ground::Coast);
    CHECK(ground.at(2, 2) == Ground::Water && ground.at(137, 67) == Ground::Water);
    // the lake is a quarter of a degree each way from the middle, a fraction of the island's area
    const int land = count(ground, Ground::Land) + count(ground, Ground::Coast);
    double c0, r0, c1, r1;
    view.to_cell({-1, -1}, c0, r0);
    view.to_cell({1, 1}, c1, r1);
    const double island = (c1 - c0) * (r0 - r1);
    view.to_cell({-0.25, -0.25}, c0, r0);
    view.to_cell({0.25, 0.25}, c1, r1);
    const double lake = (c1 - c0) * (r0 - r1);
    CHECK_NEAR(land, island - lake, island * 0.06);

    // every cell is land or water exactly as the middle of the cell says, checked against the shapes
    int wrong = 0;
    for (int r = 0; r < ground.rows(); r++)
        for (int c = 0; c < ground.cols(); c++) {
            const GeoPoint p = view.at(c + 0.5, r + 0.5);
            const bool in_island = std::fabs(p.lat) < 1 && std::fabs(p.lon) < 1;
            const bool in_lake = std::fabs(p.lat) < 0.25 && std::fabs(p.lon) < 0.25;
            const bool should_be_land = in_island && !in_lake;
            if ((ground.at(c, r) != Ground::Water) != should_be_land) wrong++;
        }
    CHECK(wrong <= 2);

    // coast: land with water next to it, and nothing else
    for (int r = 0; r < ground.rows(); r++)
        for (int c = 0; c < ground.cols(); c++) {
            auto water_at = [&](int cc, int rr) { return cc >= 0 && rr >= 0 && cc < ground.cols() && rr < ground.rows() && ground.at(cc, rr) == Ground::Water; };
            const bool near_water = water_at(c - 1, r) || water_at(c + 1, r) || water_at(c, r - 1) || water_at(c, r + 1);
            if (ground.at(c, r) == Ground::Coast) CHECK(near_water);
            if (ground.at(c, r) == Ground::Land) CHECK(!near_water);
        }
    CHECK(count(ground, Ground::Coast) > 40);

    // turned a quarter turn, a square island and its lake look the same (to within a cell or so)
    vtw::View turned = view;
    turned.set_heading(90);
    GroundMap again(map);
    again.update(turned);
    CHECK(std::abs((count(again, Ground::Land) + count(again, Ground::Coast)) - land) <= land / 20);

    // moving the view moves the picture: the island's cells shift with it
    vtw::View moved = view;
    moved.pan_cells(10, 0);
    GroundMap shifted(map);
    shifted.update(moved);
    CHECK(shifted.at(70 - 10, 35) == Ground::Water);             // where the lake's middle went
    CHECK(shifted.at(70 - 10 + 12, 35) != Ground::Water);

    // a view of only water, and a view of only land
    GroundMap sea(map);
    sea.update(make_view(40, 100, 300));
    CHECK(count(sea, Ground::Water) == sea.cols() * sea.rows());
    vtw::View inland = make_view(0.7, 0.7, 20, 100, 40);         // inside the island, outside the lake
    GroundMap solid(map);
    solid.update(inland);
    CHECK(count(solid, Ground::Land) + count(solid, Ground::Coast) == solid.cols() * solid.rows());
    CHECK(count(solid, Ground::Coast) == 0);

    // no map at all: everything is water and no level is chosen
    BaseMap nothing;
    GroundMap empty(nothing);
    empty.update(view);
    CHECK(empty.detail() == -1 && count(empty, Ground::Water) == empty.cols() * empty.rows());
}

// cells flagged with a feature
int flagged(const GroundMap& g, uint8_t bit) {
    int n = 0;
    for (int r = 0; r < g.rows(); r++) for (int c = 0; c < g.cols(); c++) n += (g.features_at(c, r) & bit) != 0;
    return n;
}

void trace_tests() {
    // a land square with a river, a state border and a country border over it
    BaseMap map;
    map.layers.push_back(layer_of(LayerKind::Land, 2, WORLD, {polygon_of({square(-2, -2, 2, 2)})}));
    const std::vector<std::pair<double, double>> river = {{-1.5, 0.3}, {-0.5, 0.9}, {0.5, 0.2}, {1.5, 0.6}};
    const std::vector<std::pair<double, double>> state = {{-1.0, -1.5}, {1.0, 1.5}};
    const std::vector<std::pair<double, double>> country = {{0.7, -1.8}, {0.7, 1.8}};
    map.layers.push_back(lines_of(LayerKind::River, 2, WORLD, {map_line(river)}));
    map.layers.push_back(lines_of(LayerKind::State, 2, WORLD, {map_line(state)}));
    map.layers.push_back(lines_of(LayerKind::Country, 2, WORLD, {map_line(country)}));
    vtw::View view = make_view(0, 0, 500, 120, 50);
    GroundMap ground(map);
    ground.update(view);

    // every line: each point along it lies in a cell with its bit, and every cell with the bit is
    // within a cell of the line (so lines are neither broken nor smeared)
    const struct { const std::vector<std::pair<double, double>>* line; uint8_t bit; } checks[] = {
        {&river, vtw::FEATURE_RIVER}, {&state, vtw::FEATURE_STATE}, {&country, vtw::FEATURE_COUNTRY}};
    for (const auto& check : checks) {
        const auto& pts = *check.line;
        for (size_t k = 0; k + 1 < pts.size(); k++)
            for (int i = 0; i <= 300; i++) {
                const double t = i / 300.0;
                double col, row;
                view.to_cell({pts[k].second + t * (pts[k + 1].second - pts[k].second), pts[k].first + t * (pts[k + 1].first - pts[k].first)}, col, row);
                bool near = false;                              // a cell the line only clips at a corner may be left out
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++) {
                        const int c = (int)col + dc, r = (int)row + dr;
                        if (c >= 0 && r >= 0 && c < ground.cols() && r < ground.rows()) near = near || (ground.features_at(c, r) & check.bit) != 0;
                    }
                CHECK(near);
                CHECK((ground.features_at((int)col, (int)row) & check.bit) != 0 || near);
            }
        int far = 0;
        for (int r = 0; r < ground.rows(); r++)
            for (int c = 0; c < ground.cols(); c++) {
                if (!(ground.features_at(c, r) & check.bit)) continue;
                double best = 1e18;                             // distance in cells from this cell's middle to the line
                for (size_t k = 0; k + 1 < pts.size(); k++) {
                    double ax, ay, bx, by;
                    view.to_cell({pts[k].second, pts[k].first}, ax, ay);
                    view.to_cell({pts[k + 1].second, pts[k + 1].first}, bx, by);
                    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
                    double t = ((c + 0.5 - ax) * dx + (r + 0.5 - ay) * dy) / l2;
                    t = std::min(std::max(t, 0.0), 1.0);
                    best = std::min(best, std::hypot(c + 0.5 - (ax + t * dx), r + 0.5 - (ay + t * dy)));
                }
                if (best > 1.0) far++;
            }
        CHECK(far == 0);
        CHECK(flagged(ground, check.bit) > 20);
        // the flagged cells make one unbroken line (each touches the next, diagonally at least)
        std::vector<int> seen((size_t)ground.cols() * ground.rows(), 0);
        int parts = 0;
        for (int r0 = 0; r0 < ground.rows(); r0++)
            for (int c0 = 0; c0 < ground.cols(); c0++) {
                if (!(ground.features_at(c0, r0) & check.bit) || seen[(size_t)r0 * ground.cols() + c0]) continue;
                parts++;
                std::vector<std::pair<int, int>> stack{{c0, r0}};
                seen[(size_t)r0 * ground.cols() + c0] = 1;
                while (!stack.empty()) {
                    const auto [c, r] = stack.back();
                    stack.pop_back();
                    for (int dr = -1; dr <= 1; dr++)
                        for (int dc = -1; dc <= 1; dc++) {
                            const int cc = c + dc, rr = r + dr;
                            if (cc < 0 || rr < 0 || cc >= ground.cols() || rr >= ground.rows()) continue;
                            if (!(ground.features_at(cc, rr) & check.bit) || seen[(size_t)rr * ground.cols() + cc]) continue;
                            seen[(size_t)rr * ground.cols() + cc] = 1;
                            stack.push_back({cc, rr});
                        }
                }
            }
        CHECK(parts == 1);
    }

    // a line that runs far past the screen on both sides is cut to it, and a line nowhere near is ignored
    BaseMap long_map;
    long_map.layers.push_back(layer_of(LayerKind::Land, 2, WORLD, {polygon_of({square(-2, -2, 2, 2)})}));
    long_map.layers.push_back(lines_of(LayerKind::River, 2, WORLD, {map_line({{-170, 0.2}, {170, 0.2}}), map_line({{100, 60}, {120, 61}})}));
    GroundMap long_ground(long_map);
    long_ground.update(view);
    CHECK(flagged(long_ground, vtw::FEATURE_RIVER) >= view.cols() && flagged(long_ground, vtw::FEATURE_RIVER) <= view.cols() * 2);

    // a line just outside the screen, in the margin that is still looked at, leaves no mark on it
    {
        BaseMap margin;
        margin.layers.push_back(layer_of(LayerKind::Land, 2, WORLD, {polygon_of({square(-2, -2, 2, 2)})}));
        const double east_lon = view.at(view.cols() * 1.025, 25).lon;     // 2.5 % of the width past the right edge
        const double south_lat = view.at(60, view.rows() * 1.03).lat;     // and past the bottom
        margin.layers.push_back(lines_of(LayerKind::River, 2, WORLD,
            {map_line({{east_lon, -1}, {east_lon, 1}}), map_line({{-1, south_lat}, {1, south_lat}})}));
        GroundMap outside(margin);
        outside.update(view);
        CHECK(flagged(outside, vtw::FEATURE_RIVER) == 0);
    }

    // a heading turn turns the lines with the picture
    vtw::View turned = view;
    turned.set_heading(90);
    GroundMap again(map);
    again.update(turned);
    CHECK(std::abs(flagged(again, vtw::FEATURE_COUNTRY) - flagged(ground, vtw::FEATURE_COUNTRY) * 1) <= flagged(ground, vtw::FEATURE_COUNTRY));
    CHECK(flagged(again, vtw::FEATURE_COUNTRY) > 10);
}

// roads show only when you are close enough, and from the one level of detail that has them
void road_zoom_tests() {
    BaseMap map;
    const GeoBox america{5, -170, 75, -50};
    map.layers.push_back(layer_of(LayerKind::Land, 0, WORLD, {polygon_of({square(-80, -170, 80, 170)})}));
    map.layers.push_back(layer_of(LayerKind::Land, 1, WORLD, {polygon_of({square(-80, -170, 80, 170)})}));
    map.layers.push_back(layer_of(LayerKind::Land, 2, america, {polygon_of({square(10, -160, 70, -60)})}));
    map.layers.push_back(lines_of(LayerKind::Highway, 2, america, {map_line({{-100.5, 40}, {-99.5, 40}})}));
    map.layers.push_back(lines_of(LayerKind::Road, 2, america, {map_line({{-100.5, 40.2}, {-99.5, 40.2}})}));
    // too far out: neither kind; a little closer: highways only; close: both
    for (double km : {3000.0, 1200.0}) {
        GroundMap g(map);
        g.update(make_view(40.1, -100, km, 100, 40));
        CHECK(flagged(g, vtw::FEATURE_HIGHWAY) == 0 && flagged(g, vtw::FEATURE_ROAD) == 0);
    }
    for (double km : {900.0, 400.0, 300.0}) {
        GroundMap g(map);
        g.update(make_view(40.1, -100, km, 100, 40));
        CHECK(flagged(g, vtw::FEATURE_ROAD) == 0);
    }
    for (double km : {300.0, 200.0, 50.0}) {
        GroundMap g(map);
        g.update(make_view(40.1, -100, km, 100, 40));
        CHECK(flagged(g, vtw::FEATURE_HIGHWAY) > 0);
        CHECK((flagged(g, vtw::FEATURE_ROAD) > 0) == (km < 250));
        CHECK(flagged(g, vtw::FEATURE_RIVER | vtw::FEATURE_STATE | vtw::FEATURE_COUNTRY) == 0);      // roads set no other bits
    }
}

void detail_tests() {
    // three levels; the finest covers only a box
    BaseMap map;
    const GeoBox fine_box{20, -130, 55, -60};
    map.layers.push_back(layer_of(LayerKind::Land, 0, WORLD, {polygon_of({square(-1, -1, 1, 1)})}));
    map.layers.push_back(layer_of(LayerKind::Land, 1, WORLD, {polygon_of({square(-1, -1, 1, 1)})}));
    map.layers.push_back(layer_of(LayerKind::Land, 2, fine_box, {polygon_of({square(30, -100, 31, -99)})}));
    // zoom picks the level
    CHECK(vtw::choose_detail(map, make_view(35, -95, 3000)) == 0);
    CHECK(vtw::choose_detail(map, make_view(35, -95, 1500)) == 0);       // at the limit: still the coarse level
    CHECK(vtw::choose_detail(map, make_view(35, -95, 1499)) == 1);
    CHECK(vtw::choose_detail(map, make_view(35, -95, 300)) == 1);
    CHECK(vtw::choose_detail(map, make_view(35, -95, 199)) == 2);
    CHECK(vtw::choose_detail(map, make_view(35, -95, 5)) == 2);
    // outside the fine layer's area a close view falls back to the next level
    CHECK(vtw::choose_detail(map, make_view(-20, 100, 20)) == 1);
    CHECK(vtw::choose_detail(map, make_view(56, -95, 20)) == 1);       // a view that is partly outside
    CHECK(vtw::choose_detail(map, make_view(55.1, -95, 400)) == 1);
    // no land layers at all
    BaseMap none;
    none.layers.push_back(layer_of(LayerKind::Lake, 2, WORLD, {}));
    CHECK(vtw::choose_detail(none, make_view(35, -95, 20)) == -1);
}

// ---- real places on the real file

void real_tests() {
    BaseMap map;
    std::string error;
    const std::string path = vtw::find_basemap_file(std::string(VTW_DATA_DIR) + "/basemap.bin");
    CHECK(!path.empty());
    if (path.empty() || !vtw::load_basemap(path, map, error)) { std::printf("  %s\n", error.c_str()); CHECK(false); return; }
    CHECK(map.places.size() > 2000);                            // cities too
    CHECK(map.layers.size() == 17);                          // five layers at each of three levels, and two kinds of road

    struct Place { const char* name; double lat, lon; Ground ground; };
    const Place places[] = {
        {"Wichita, Kansas", 37.69, -97.34, Ground::Land},
        {"Denver", 39.74, -104.99, Ground::Land},
        {"Chicago (inland)", 41.9, -88.5, Ground::Land},
        {"Central Florida", 28.0, -81.5, Ground::Land},
        {"Gulf of Mexico", 25.0, -90.0, Ground::Water},
        {"Atlantic, off Carolina", 33.0, -72.0, Ground::Water},
        {"Pacific, off California", 35.0, -126.0, Ground::Water},
        {"Lake Michigan", 43.9, -87.0, Ground::Water},
        {"Lake Superior", 47.7, -87.5, Ground::Water},
        {"Lake Erie", 42.2, -81.2, Ground::Water},
        {"Hudson Bay", 58.0, -85.0, Ground::Water},
    };
    // each place is checked at all three levels of detail, by the cell under the middle of the view
    for (const Place& place : places)
        for (double km : {3000.0, 600.0, 60.0}) {
            vtw::View view = make_view(place.lat, place.lon, km, 100, 40);
            GroundMap ground(map);
            ground.update(view);
            const Ground got = ground.at(50, 20);
            const bool same = place.ground == Ground::Land ? got != Ground::Water : got == Ground::Water;
            if (!same) std::printf("  %s at %.0f km (detail %d): got %d\n", place.name, km, ground.detail(), (int)got);
            CHECK(same);
        }

    // the level in use follows the zoom
    CHECK(vtw::choose_detail(map, make_view(39, -97, 3000)) == 0);
    CHECK(vtw::choose_detail(map, make_view(39, -97, 600)) == 1);
    CHECK(vtw::choose_detail(map, make_view(39, -97, 60)) == 2);
    CHECK(vtw::choose_detail(map, make_view(50, 10, 60)) == 1);        // Europe: the fine level covers North America only

    // borders and rivers that really are there
    auto features_near_middle = [&](double lat, double lon, double km, uint8_t bit, int half_cols = 6, int half_rows = 4) {
        GroundMap g(map);
        g.update(make_view(lat, lon, km, 100, 40));
        int n = 0;
        for (int r = std::max(0, 20 - half_rows); r <= std::min(39, 20 + half_rows); r++)
            for (int c = std::max(0, 50 - half_cols); c <= std::min(99, 50 + half_cols); c++) n += (g.features_at(c, r) & bit) != 0;
        return n;
    };
    CHECK(features_near_middle(49.0, -100.0, 100, vtw::FEATURE_COUNTRY) > 5);       // the US-Canada border, 49 N
    CHECK(features_near_middle(37.0, -98.0, 100, vtw::FEATURE_STATE) > 5);          // Kansas and Oklahoma
    CHECK(features_near_middle(39.0, -102.05, 100, vtw::FEATURE_STATE) > 5);        // Kansas and Colorado
    CHECK(features_near_middle(32.0, -100.0, 3000, vtw::FEATURE_COUNTRY, 50, 20) > 10);       // Mexico, at the coarse level
    CHECK(features_near_middle(38.65, -90.18, 40, vtw::FEATURE_RIVER, 50, 20) > 10);          // the Mississippi at St. Louis
    CHECK(features_near_middle(39.0, -97.0, 3000, vtw::FEATURE_STATE, 50, 20) > 20);          // state lines, coarse
    CHECK(features_near_middle(47.6, -122.33, 60, vtw::FEATURE_HIGHWAY, 25, 10) > 8);          // Interstate 5 through Seattle
    CHECK(features_near_middle(37.7, -97.3, 120, vtw::FEATURE_HIGHWAY, 40, 15) > 20);          // highways around Wichita
    CHECK(features_near_middle(37.7, -97.3, 150, vtw::FEATURE_ROAD, 40, 15) > 20);             // and the other highways
    CHECK(features_near_middle(37.7, -97.3, 1500, vtw::FEATURE_HIGHWAY, 50, 20) == 0);         // none when zoomed out
    CHECK(features_near_middle(37.7, -97.3, 600, vtw::FEATURE_ROAD, 50, 20) == 0);             // other highways only when close
    CHECK(features_near_middle(37.7, -97.3, 600, vtw::FEATURE_HIGHWAY, 50, 20) > 20);
    {
        GroundMap ocean(map);
        ocean.update(make_view(35, -140, 600, 100, 40));                               // the open Pacific
        CHECK(flagged(ocean, 31) == 0);
    }

    // how much land a view of the continental US shows: a good part of it, not all and not none
    GroundMap us(map);
    us.update(make_view(39, -97, 4500, 160, 60));
    const int total = us.cols() * us.rows();
    const int land = count(us, Ground::Land) + count(us, Ground::Coast);
    CHECK(land > total * 0.40 && land < total * 0.85);

    // how long a view takes to work out, at each level (not strict: a very slow machine is not a bug)
    for (double km : {4000.0, 600.0, 60.0}) {
        const auto start = std::chrono::steady_clock::now();
        GroundMap g(map);
        g.update(make_view(39, -97, km, 200, 60));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %.0f km across, detail %d: %.1f ms\n", km, g.detail(), ms);
        CHECK(ms < 500);
    }
}

}  // namespace

int main() {
    file_tests();
    simplify_tests();
    simplify_line_tests();
    fill_tests();
    trace_tests();
    road_zoom_tests();
    detail_tests();
    real_tests();
    if (failures == 0) std::printf("all base map tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
