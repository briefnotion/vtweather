// GroundMap: which cells of the screen are land, water or coast, worked out from the BaseMap.
// The polygons are projected through the View and filled into the grid of cells (a scan line
// across every row, even-odd rule, so holes work); lakes are then cut out of the land, and a land
// cell with water next to it becomes coast. Rivers, borders and roads, which are lines, are traced
// through the cells they cross. It is worked out again only when the view changes,
// not every frame.
//
// The level of detail follows the zoom: the coarse world map for views of thousands of
// kilometres, the finer ones as you come closer, as far as a level covers what is on screen.
#pragma once

#include <vector>

#include "basemap.h"
#include "view.h"

namespace vtw {

enum class Ground : uint8_t { Water = 0, Land = 1, Coast = 2 };

// Zoom limits between the levels of detail, in km across the screen.
constexpr double MEDIUM_DETAIL_BELOW_KM = 1500;
constexpr double FINE_DETAIL_BELOW_KM = 200;

// What runs through a cell besides land and water: a river, a state or province border, a country
// border, a major highway, another highway (any mix of them, as bits).
constexpr uint8_t FEATURE_RIVER = 1, FEATURE_STATE = 2, FEATURE_COUNTRY = 4, FEATURE_HIGHWAY = 8, FEATURE_ROAD = 16;

// Roads are shown only when you are close enough for them to matter (km across the screen). They
// exist at one level of detail only (North America), whatever level the land is drawn at.
constexpr double HIGHWAYS_BELOW_KM = 1000;
constexpr double ROADS_BELOW_KM = 250;

// The level of detail to use for a view: the one the zoom asks for, or the next coarser one
// until a level whose land layer covers everything on screen. -1 if the map has no land layer.
int choose_detail(const BaseMap& map, const View& view);

class GroundMap {
public:
    explicit GroundMap(const BaseMap& map) : map_(map) {}

    void update(const View& view);               // worked out again only if the view changed

    int detail() const { return detail_; }       // the level in use, -1 for none
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    Ground at(int col, int row) const { return (Ground)cells_[(size_t)row * cols_ + col]; }
    uint8_t features_at(int col, int row) const { return features_[(size_t)row * cols_ + col]; }

private:
    const BaseMap& map_;
    std::vector<uint8_t> cells_, features_;
    int cols_ = 0, rows_ = 0, detail_ = -1;
    bool have_ = false;
    GeoPoint centre_;
    double zoom_ = 0, aspect_ = 0, heading_ = 0;
};

}  // namespace vtw
