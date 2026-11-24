// make_basemap: turn Natural Earth's GeoJSON files into the compact base map file the program
// reads (data/basemap.bin). Run once, when the map data changes:
//
//   ./tools/get_natural_earth.sh                       (downloads the files, once)
//   ./build/make_basemap tools/natural_earth data/basemap.bin
//
// For each of three levels of detail (110m, 50m, 10m) it reads the land and lake polygons and the
// country borders, state and province borders and rivers as lines, cuts the finest level down to
// North America (the radar covers only the United States and the finest level is the largest) and
// reads the cities (the 10m file inside North America, the 50m file elsewhere),
// drops points that do not matter at that level (Douglas-Peucker) and writes everything as whole
// numbers of 1e-5 degrees.
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

// Liang-Barsky: the part of a segment inside the box, if any (in the same longitude, latitude units).
bool clip_segment(Point& a, Point& b, const vtw::GeoBox& box) {
    double t0 = 0, t1 = 1;
    const double dx = b.first - a.first, dy = b.second - a.second;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.first - box.west, box.east - a.first, a.second - box.south, box.north - a.second};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0) { if (t > t1) return false; t0 = std::max(t0, t); }
            else { if (t < t0) return false; t1 = std::min(t1, t); }
        }
    }
    const Point from{a.first + t0 * dx, a.second + t0 * dy}, to{a.first + t1 * dx, a.second + t1 * dy};
    a = from;
    b = to;
    return true;
}

// One GeoJSON line -> the pieces of it inside the level's area, each simplified, in map units.
std::vector<std::vector<int32_t>> convert_line(const Json& line, const Level& level) {
    std::vector<Point> points;
    for (const Json& p : line) points.push_back({p[0].get<double>(), p[1].get<double>()});
    std::vector<std::vector<Point>> pieces;
    if (whole_world(level.coverage)) {
        pieces.push_back(points);
    } else {
        std::vector<Point> current;
        for (size_t i = 0; i + 1 < points.size(); i++) {
            Point a = points[i], b = points[i + 1];
            if (!clip_segment(a, b, level.coverage)) {
                if (current.size() > 1) pieces.push_back(current);
                current.clear();
                continue;
            }
            if (current.empty() || current.back() != a) {         // a new piece when the line left the area
                if (current.size() > 1) pieces.push_back(current);
                current.assign(1, a);
            }
            current.push_back(b);
        }
        if (current.size() > 1) pieces.push_back(current);
    }
    std::vector<std::vector<int32_t>> out;
    for (const std::vector<Point>& piece : pieces) {
        std::vector<int32_t> units;
        for (const Point& p : piece) {
            units.push_back((int32_t)std::lround(p.first * vtw::MAP_UNITS_PER_DEGREE));
            units.push_back((int32_t)std::lround(p.second * vtw::MAP_UNITS_PER_DEGREE));
        }
        units = vtw::simplify_line(units, level.tolerance);
        if (units.size() >= 4) out.push_back(std::move(units));
    }
    return out;
}

void add_line(const Json& coordinates, const Level& level, vtw::MapLayer& layer) {
    for (std::vector<int32_t>& units : convert_line(coordinates, level)) {
        vtw::MapLine line;
        int32_t west = units[0], east = units[0], south = units[1], north = units[1];
        for (size_t i = 0; i < units.size(); i += 2) {
            west = std::min(west, units[i]); east = std::max(east, units[i]);
            south = std::min(south, units[i + 1]); north = std::max(north, units[i + 1]);
        }
        line.box = {south / vtw::MAP_UNITS_PER_DEGREE, west / vtw::MAP_UNITS_PER_DEGREE,
                    north / vtw::MAP_UNITS_PER_DEGREE, east / vtw::MAP_UNITS_PER_DEGREE};
        line.points = std::move(units);
        layer.lines.push_back(std::move(line));
    }
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
                vtw::BaseMap& map, const char* file_part = nullptr) {
    const std::string path = folder + "/ne_" + level.name + "_" + (file_part ? file_part : what) + ".geojson";
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
        else if (type == "LineString") add_line(geometry["coordinates"], level, layer);
        else if (type == "MultiLineString")
            for (const Json& line : geometry["coordinates"]) add_line(line, level, layer);
    }
    size_t points = 0;
    for (const vtw::MapPolygon& p : layer.polygons)
        for (const std::vector<int32_t>& r : p.rings) points += r.size() / 2;
    for (const vtw::MapLine& l : layer.lines) points += l.points.size() / 2;
    std::printf("%-5s %-8s %6zu shapes %8zu points\n", level.name, what, layer.polygons.size() + layer.lines.size(), points);
    map.layers.push_back(std::move(layer));
    return true;
}

// Cities from Natural Earth's populated places: the 10m file inside North America (the finest
// level's area), the 50m file elsewhere. Names are kept as plain ASCII.
constexpr int MIN_NORTH_AMERICA_POPULATION = 5000;

bool read_places(const std::string& folder, vtw::BaseMap& map) {
    const vtw::GeoBox area = LEVELS[2].coverage;
    size_t counts[2] = {0, 0};
    for (int fine = 1; fine >= 0; fine--) {
        const std::string path = folder + "/ne_" + (fine ? "10m" : "50m") + "_populated_places.geojson";
        std::ifstream file(path);
        if (!file) { std::fprintf(stderr, "cannot open %s (run tools/get_natural_earth.sh)\n", path.c_str()); return false; }
        const Json json = Json::parse(file, nullptr, false);
        if (json.is_discarded() || !json.contains("features")) { std::fprintf(stderr, "%s is not GeoJSON\n", path.c_str()); return false; }
        for (const Json& feature : json["features"]) {
            if (feature["geometry"].is_null()) continue;
            const double lon = feature["geometry"]["coordinates"][0].get<double>();
            const double lat = feature["geometry"]["coordinates"][1].get<double>();
            const bool inside = lon >= area.west && lon <= area.east && lat >= area.south && lat <= area.north;
            if (inside != (fine == 1)) continue;                  // fine file inside, coarse file outside
            const Json& props = feature["properties"];
            const long population = props["POP_MAX"].is_number() ? props["POP_MAX"].get<long>() : 0;
            if (fine && population < MIN_NORTH_AMERICA_POPULATION) continue;
            std::string name;
            for (char c : props["NAMEASCII"].get<std::string>())
                if (c >= 32 && c < 127) name += c;                // plain ASCII only
            if (name.empty()) continue;
            vtw::MapPlace place;
            place.lon = (int32_t)std::lround(lon * vtw::MAP_UNITS_PER_DEGREE);
            place.lat = (int32_t)std::lround(lat * vtw::MAP_UNITS_PER_DEGREE);
            place.population = (uint32_t)std::max(0L, population);
            place.name = name.substr(0, 60);
            map.places.push_back(std::move(place));
            counts[fine]++;
        }
    }
    std::printf("places: %zu in North America (10m), %zu elsewhere (50m)\n", counts[1], counts[0]);
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
        if (!read_layer(argv[1], level, "countries", vtw::LayerKind::Country, map, "admin_0_boundary_lines_land")) return 1;
        if (!read_layer(argv[1], level, "states", vtw::LayerKind::State, map, "admin_1_states_provinces_lines")) return 1;
        if (!read_layer(argv[1], level, "rivers", vtw::LayerKind::River, map, "rivers_lake_centerlines")) return 1;
    }
    if (!read_places(argv[1], map)) return 1;
    std::string error;
    if (!vtw::save_basemap(argv[2], map, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    std::printf("wrote %s (%zu bytes)\n", argv[2], vtw::write_basemap(map).size());
    return 0;
}
