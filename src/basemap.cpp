#include "basemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace vtw {

namespace {

constexpr char MAGIC[8] = {'V', 'T', 'W', 'M', 'A', 'P', '1', '\n'};
constexpr uint32_t MAX_LAYERS = 64;
constexpr uint32_t MAX_POLYGONS = 2000000;
constexpr uint32_t MAX_RINGS = 1000000;
constexpr uint64_t MAX_POINTS = 20000000;

struct Reader {
    const uint8_t* data;
    size_t size, pos = 0;
    bool bad = false;

    size_t left() const { return size - pos; }
    uint32_t u32() {
        if (left() < 4) { bad = true; return 0; }
        const uint32_t v = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) | ((uint32_t)data[pos + 2] << 16) |
                           ((uint32_t)data[pos + 3] << 24);
        pos += 4;
        return v;
    }
    int32_t i32() { return (int32_t)u32(); }
    uint8_t u8() {
        if (left() < 1) { bad = true; return 0; }
        return data[pos++];
    }
};

double degrees(int32_t units) { return units / MAP_UNITS_PER_DEGREE; }

GeoBox read_box(Reader& in) {
    GeoBox b;
    b.south = degrees(in.i32());
    b.west = degrees(in.i32());
    b.north = degrees(in.i32());
    b.east = degrees(in.i32());
    return b;
}

void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF);
}
void put_i32(std::string& s, int32_t v) { put32(s, (uint32_t)v); }
int32_t units(double degrees_) { return (int32_t)std::lround(degrees_ * MAP_UNITS_PER_DEGREE); }
void put_box(std::string& s, const GeoBox& b) {
    put_i32(s, units(b.south));
    put_i32(s, units(b.west));
    put_i32(s, units(b.north));
    put_i32(s, units(b.east));
}

}  // namespace

bool read_basemap(const uint8_t* data, size_t size, BaseMap& map, std::string& error) {
    map = BaseMap();
    auto fail = [&](const char* why) { error = why; map = BaseMap(); return false; };
    if (size < sizeof MAGIC || std::memcmp(data, MAGIC, sizeof MAGIC) != 0) return fail("not a base map file");

    Reader in{data, size, sizeof MAGIC};
    const uint32_t layers = in.u32();
    if (in.bad || layers > MAX_LAYERS) return fail("the base map file is damaged");
    uint64_t points = 0;
    for (uint32_t l = 0; l < layers; l++) {
        MapLayer layer;
        const uint8_t kind = in.u8(), detail = in.u8();
        in.u8(); in.u8();
        if (kind > 1 || detail >= DETAIL_LEVELS) return fail("the base map file is damaged");
        layer.kind = (LayerKind)kind;
        layer.detail = detail;
        layer.coverage = read_box(in);
        const uint32_t polygons = in.u32();
        if (in.bad || polygons > MAX_POLYGONS || (uint64_t)polygons * 20 > in.left()) return fail("the base map file is damaged");
        layer.polygons.reserve(polygons);
        for (uint32_t p = 0; p < polygons; p++) {
            MapPolygon polygon;
            polygon.box = read_box(in);
            const uint32_t rings = in.u32();
            if (in.bad || rings > MAX_RINGS || (uint64_t)rings * 4 > in.left()) return fail("the base map file is damaged");
            polygon.rings.reserve(rings);
            for (uint32_t r = 0; r < rings; r++) {
                const uint32_t count = in.u32();
                points += count;
                if (in.bad || points > MAX_POINTS || (uint64_t)count * 8 > in.left()) return fail("the base map file is damaged");
                std::vector<int32_t> ring((size_t)count * 2);
                for (int32_t& v : ring) v = in.i32();
                polygon.rings.push_back(std::move(ring));
            }
            layer.polygons.push_back(std::move(polygon));
        }
        if (in.bad) return fail("the base map file is damaged");
        map.layers.push_back(std::move(layer));
    }
    return true;
}

bool load_basemap(const std::string& path, BaseMap& map, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "cannot open " + path; map = BaseMap(); return false; }
    std::stringstream bytes;
    bytes << file.rdbuf();
    const std::string text = bytes.str();
    return read_basemap((const uint8_t*)text.data(), text.size(), map, error);
}

std::string find_basemap_file(const std::string& given) {
    std::vector<std::string> candidates;
    if (!given.empty()) return std::ifstream(given).good() ? given : "";
    candidates.push_back("data/basemap.bin");
    candidates.push_back("../data/basemap.bin");
#ifdef VTW_SOURCE_DIR
    candidates.push_back(std::string(VTW_SOURCE_DIR) + "/data/basemap.bin");
#endif
    for (const std::string& path : candidates)
        if (std::ifstream(path).good()) return path;
    return "";
}

std::string write_basemap(const BaseMap& map) {
    std::string s(MAGIC, sizeof MAGIC);
    put32(s, (uint32_t)map.layers.size());
    for (const MapLayer& layer : map.layers) {
        s += (char)layer.kind;
        s += (char)layer.detail;
        s += '\0';
        s += '\0';
        put_box(s, layer.coverage);
        put32(s, (uint32_t)layer.polygons.size());
        for (const MapPolygon& polygon : layer.polygons) {
            put_box(s, polygon.box);
            put32(s, (uint32_t)polygon.rings.size());
            for (const std::vector<int32_t>& ring : polygon.rings) {
                put32(s, (uint32_t)(ring.size() / 2));
                for (int32_t v : ring) put_i32(s, v);
            }
        }
    }
    return s;
}

bool save_basemap(const std::string& path, const BaseMap& map, std::string& error) {
    const std::string bytes = write_basemap(map);
    std::ofstream file(path, std::ios::binary);
    if (!file || !file.write(bytes.data(), (std::streamsize)bytes.size())) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

std::vector<int32_t> simplify_ring(const std::vector<int32_t>& ring, double tolerance_degrees) {
    const size_t n = ring.size() / 2;
    if (n < 3) return {};
    if (tolerance_degrees <= 0 || n < 5) return ring;

    const double tolerance = tolerance_degrees * MAP_UNITS_PER_DEGREE;
    auto x = [&](size_t i) { return (double)ring[2 * i]; };
    auto y = [&](size_t i) { return (double)ring[2 * i + 1]; };
    // the distance of point p from the line through a and b
    auto distance = [&](size_t p, size_t a, size_t b) {
        const double dx = x(b) - x(a), dy = y(b) - y(a);
        const double length2 = dx * dx + dy * dy;
        if (length2 == 0) return std::hypot(x(p) - x(a), y(p) - y(a));
        double t = ((x(p) - x(a)) * dx + (y(p) - y(a)) * dy) / length2;
        t = std::min(std::max(t, 0.0), 1.0);
        return std::hypot(x(p) - (x(a) + t * dx), y(p) - (y(a) + t * dy));
    };

    // a closed ring: start from the point 0 and the point furthest from it
    size_t far = 0;
    double most = -1;
    for (size_t i = 1; i < n; i++) {
        const double d = std::hypot(x(i) - x(0), y(i) - y(0));
        if (d > most) { most = d; far = i; }
    }
    std::vector<bool> keep(n, false);
    keep[0] = keep[far] = true;
    std::vector<std::pair<size_t, size_t>> work;
    work.push_back({0, far});
    work.push_back({far, n});                  // n stands for point 0 again, closing the ring
    while (!work.empty()) {
        const auto [a, b] = work.back();
        work.pop_back();
        const size_t bb = b % n;
        double worst = -1;
        size_t at = 0;
        for (size_t i = a + 1; i < b; i++) {
            const double d = distance(i, a, bb);
            if (d > worst) { worst = d; at = i; }
        }
        if (worst > tolerance) {
            keep[at] = true;
            work.push_back({a, at});
            work.push_back({at, b});
        }
    }
    std::vector<int32_t> out;
    for (size_t i = 0; i < n; i++)
        if (keep[i]) { out.push_back(ring[2 * i]); out.push_back(ring[2 * i + 1]); }
    return out.size() >= 6 ? out : std::vector<int32_t>();
}

}  // namespace vtw
