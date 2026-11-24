// make_basemap: turn Natural Earth's GeoJSON files into the compact base map file the program
// reads (data/basemap.bin). Run once, when the map data changes:
//
//   ./tools/get_natural_earth.sh                       (downloads the files, once)
//   ./build/make_basemap tools/natural_earth data/basemap.bin
//
// For each of three levels of detail (110m, 50m, 10m) it reads the land and lake polygons,
// cuts the finest level down to North America (the radar covers only the United States and the
// finest level is the largest), drops points that do not matter at that level (Douglas-Peucker)
// and writes everything as whole numbers of 1e-5 degrees.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "../src/basemap.h"

namespace {

using Json = nlohmann::json;
using Point = std::pair<double, double>;       // longitude, latitude

struct Level {
    const char* name;
    int detail;
    double tolerance;          // degrees: how far a simplified edge may move from the original
    vtw::GeoBox coverage;      // south, west, north, east
};

const Level LEVELS[] = {
    {"110m", 0, 0.0, {-90, -180, 90, 180}},
    {"50m", 1, 0.004, {-90, -180, 90, 180}},
    {"10m", 2, 0.0015, {5, -170, 75, -50}},
};

bool whole_world(const vtw::GeoBox& b) { return b.south <= -90 && b.north >= 90 && b.west <= -180 && b.east >= 180; }

// Sutherland-Hodgman: keep the part of a ring that is inside one edge of the box.
std::vector<Point> clip_edge(const std::vector<Point>& in, int edge, double limit) {
    // edge: 0 west (x >= limit), 1 east (x <= limit), 2 south (y >= limit), 3 north (y <= limit)
    auto inside = [&](const Point& p) {
        switch (edge) {
            case 0: return p.first >= limit;
            case 1: return p.first <= limit;
            case 2: return p.second >= limit;
            default: return p.second <= limit;
        }
    };
    auto cross = [&](const Point& a, const Point& b) {
        if (edge < 2) {
            const double t = (limit - a.first) / (b.first - a.first);
            return Point{limit, a.second + t * (b.second - a.second)};
        }
        const double t = (limit - a.second) / (b.second - a.second);
        return Point{a.first + t * (b.first - a.first), limit};
    };
    std::vector<Point> out;
    for (size_t i = 0; i < in.size(); i++) {
        const Point& a = in[i];
        const Point& b = in[(i + 1) % in.size()];
        if (inside(a)) {
            out.push_back(a);
            if (!inside(b)) out.push_back(cross(a, b));
        } else if (inside(b)) {
            out.push_back(cross(a, b));
        }
    }
    return out;
}

std::vector<Point> clip_ring(std::vector<Point> ring, const vtw::GeoBox& box) {
    const double limits[4] = {box.west, box.east, box.south, box.north};
    for (int edge = 0; edge < 4 && !ring.empty(); edge++) ring = clip_edge(ring, edge, limits[edge]);
    return ring;
}

// One GeoJSON ring -> a simplified ring in map units, or empty.
std::vector<int32_t> convert_ring(const Json& ring, const Level& level) {
    std::vector<Point> points;
    for (const Json& p : ring) points.push_back({p[0].get<double>(), p[1].get<double>()});
    if (points.size() > 1 && points.front() == points.back()) points.pop_back();    // the ring is closed anyway
    if (points.size() < 3) return {};
    if (!whole_world(level.coverage)) points = clip_ring(std::move(points), level.coverage);
    if (points.size() < 3) return {};
    std::vector<int32_t> units;
    for (const Point& p : points) {
        units.push_back((int32_t)std::lround(p.first * vtw::MAP_UNITS_PER_DEGREE));
        units.push_back((int32_t)std::lround(p.second * vtw::MAP_UNITS_PER_DEGREE));
    }
    return vtw::simplify_ring(units, level.tolerance);
}

void add_polygon(const Json& rings, const Level& level, vtw::MapLayer& layer) {
    vtw::MapPolygon polygon;
    for (size_t i = 0; i < rings.size(); i++) {
        std::vector<int32_t> ring = convert_ring(rings[i], level);
        if (ring.empty()) {
            if (i == 0) return;                          // no outline left: no polygon
            continue;                                    // a hole that is gone
        }
        polygon.rings.push_back(std::move(ring));
    }
    if (polygon.rings.empty()) return;
    const std::vector<int32_t>& outline = polygon.rings[0];
    int32_t west = outline[0], east = outline[0], south = outline[1], north = outline[1];
    for (size_t i = 0; i < outline.size(); i += 2) {
        west = std::min(west, outline[i]); east = std::max(east, outline[i]);
        south = std::min(south, outline[i + 1]); north = std::max(north, outline[i + 1]);
    }
    polygon.box = {south / vtw::MAP_UNITS_PER_DEGREE, west / vtw::MAP_UNITS_PER_DEGREE,
                   north / vtw::MAP_UNITS_PER_DEGREE, east / vtw::MAP_UNITS_PER_DEGREE};
    layer.polygons.push_back(std::move(polygon));
}

bool read_layer(const std::string& folder, const Level& level, const char* what, vtw::LayerKind kind,
                vtw::BaseMap& map) {
    const std::string path = folder + "/ne_" + level.name + "_" + what + ".geojson";
    std::ifstream file(path);
    if (!file) { std::fprintf(stderr, "cannot open %s (run tools/get_natural_earth.sh)\n", path.c_str()); return false; }
    const Json json = Json::parse(file, nullptr, false);
    if (json.is_discarded() || !json.contains("features")) { std::fprintf(stderr, "%s is not GeoJSON\n", path.c_str()); return false; }

    vtw::MapLayer layer;
    layer.kind = kind;
    layer.detail = level.detail;
    layer.coverage = level.coverage;
    for (const Json& feature : json["features"]) {
        const Json& geometry = feature["geometry"];
        if (geometry.is_null()) continue;
        const std::string type = geometry["type"];
        if (type == "Polygon") add_polygon(geometry["coordinates"], level, layer);
        else if (type == "MultiPolygon")
            for (const Json& polygon : geometry["coordinates"]) add_polygon(polygon, level, layer);
    }
    size_t points = 0;
    for (const vtw::MapPolygon& p : layer.polygons)
        for (const std::vector<int32_t>& r : p.rings) points += r.size() / 2;
    std::printf("%-5s %-5s %6zu polygons %8zu points\n", level.name, what, layer.polygons.size(), points);
    map.layers.push_back(std::move(layer));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s FOLDER_WITH_NATURAL_EARTH_FILES OUTPUT.bin\n", argv[0]);
        return 2;
    }
    vtw::BaseMap map;
    for (const Level& level : LEVELS) {
        if (!read_layer(argv[1], level, "land", vtw::LayerKind::Land, map)) return 1;
        if (!read_layer(argv[1], level, "lakes", vtw::LayerKind::Lake, map)) return 1;
    }
    std::string error;
    if (!vtw::save_basemap(argv[2], map, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    std::printf("wrote %s (%zu bytes)\n", argv[2], vtw::write_basemap(map).size());
    return 0;
}
