// BaseMap: the land and water of the earth as polygons, read from a compact binary file
// (data/basemap.bin). The file is made once, at development time, from Natural Earth's public
// domain map data by tools/make_basemap.cpp; the program only reads it.
//
// There are three levels of detail (about 1:110 million, 1:50 million and 1:10 million), each a
// layer of land polygons and a layer of lake polygons, with the area for which that layer is
// complete. A polygon is a list of rings; the first is its outline and any others are holes, and
// a place is inside it by the even-odd rule. Coordinates are whole numbers of 1e-5 degrees (about
// a metre).
//
// File format, little-endian:  "VTWMAP1\n"; u32 layers; for each layer: u8 kind (0 land,
// 1 lake), u8 detail (0, 1, 2), u16 0, i32 south, west, north, east of its coverage, u32 polygons;
// for each polygon: i32 south, west, north, east, u32 rings; for each ring: u32 points, then
// that many i32 pairs (longitude, latitude).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "view.h"

namespace vtw {

constexpr double MAP_UNITS_PER_DEGREE = 100000.0;

enum class LayerKind : uint8_t { Land = 0, Lake = 1 };
constexpr int DETAIL_LEVELS = 3;                 // 0 coarse (world), 1 medium, 2 fine

struct MapPolygon {
    GeoBox box;                                  // degrees
    std::vector<std::vector<int32_t>> rings;     // each: lon, lat, lon, lat ... (1e-5 degrees)
};

struct MapLayer {
    LayerKind kind = LayerKind::Land;
    int detail = 0;
    GeoBox coverage;                             // where this layer is complete, degrees
    std::vector<MapPolygon> polygons;
};

struct BaseMap {
    std::vector<MapLayer> layers;
};

// Read the file's bytes. Anything wrong (a wrong signature, a cut-off or impossible file) gives
// false and a reason; nothing is read past the end, and sizes are checked before memory is used.
bool read_basemap(const uint8_t* data, size_t size, BaseMap& map, std::string& error);
bool load_basemap(const std::string& path, BaseMap& map, std::string& error);

// Where the base map file is: the path given, else data/basemap.bin in the folder you run from,
// else ../data/basemap.bin (running from build/), else the data folder of the source tree the
// program was built from. Empty if none of them exists.
std::string find_basemap_file(const std::string& given);

// The file's bytes for a map (used by the converter and the tests).
std::string write_basemap(const BaseMap& map);
bool save_basemap(const std::string& path, const BaseMap& map, std::string& error);

// Douglas-Peucker: fewer points for a closed ring while no point moves further than the
// tolerance (in degrees). A ring that would be left with fewer than 3 points comes back empty.
std::vector<int32_t> simplify_ring(const std::vector<int32_t>& ring, double tolerance_degrees);

}  // namespace vtw
