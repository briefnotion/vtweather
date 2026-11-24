#include "cell_grid.h"

#include <algorithm>

namespace vtw {

CellGrid::CellGrid(const View& view) : cols_(view.cols()), rows_(view.rows()) {
    corners_.reserve((size_t)(cols_ + 1) * (rows_ + 1));
    for (int row = 0; row <= rows_; row++)
        for (int col = 0; col <= cols_; col++) corners_.push_back(view.at(col, row));
}

GeoBox CellGrid::box(int col, int row) const {
    const GeoPoint a = corner(col, row), b = corner(col + 1, row);
    const GeoPoint c = corner(col, row + 1), d = corner(col + 1, row + 1);
    return GeoBox{std::min({a.lat, b.lat, c.lat, d.lat}), std::min({a.lon, b.lon, c.lon, d.lon}),
                  std::max({a.lat, b.lat, c.lat, d.lat}), std::max({a.lon, b.lon, c.lon, d.lon})};
}

}  // namespace vtw
