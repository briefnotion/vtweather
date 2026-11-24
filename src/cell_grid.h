// CellGrid: the earth under every cell of the screen, worked out once a frame. Several layers
// (the graticule, the rain, later the map) need to know where each cell is; they share this
// instead of each asking the View for every corner again.
#pragma once

#include <vector>

#include "view.h"

namespace vtw {

class CellGrid {
public:
    explicit CellGrid(const View& view);

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    GeoPoint corner(int col, int row) const { return corners_[(size_t)row * (cols_ + 1) + col]; }   // 0..cols, 0..rows
    GeoBox box(int col, int row) const;       // the area of a cell: the box round its four corners

private:
    int cols_, rows_;
    std::vector<GeoPoint> corners_;
};

}  // namespace vtw
