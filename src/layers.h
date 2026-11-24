// LayerBlend: how the things drawn on the map share a cell. Rivers, roads, borders, the coast, the
// grid and the rain each put letters in the cells they cover. A cell with one of them holds its
// letter still (shimmer 0). Where they overlap the cell shimmers: OVERLAP_SHIMMER for each layer
// beyond the first, so two layers shimmer at one times that rate, three at twice it, four at three
// times it and so on. That works without colour too, which is how an overlap shows there.
// The look of the cell (its brightness and colour) is the top layer's, in this order of priority,
// highest first: rain, the grid, borders, the coast, roads, rivers. The colours are not mixed.
// The ground (land and water) is the base the layers sit on and is not a layer: the first layer in a
// cell takes the place of the water's own faint letters but keeps the water's colour behind them.
// The order the layers are added in does not matter.
#pragma once

#include <cstdint>
#include <vector>

#include "vtscreen/vtscreen.h"

namespace vtw {

// In order of priority, lowest first: the higher one decides how a cell looks where they overlap.
enum class Layer : uint8_t { River = 0, Road, Coast, Border, Grid, Rain };
constexpr int LAYER_COUNT = 6;

// The base of the shimmer, in the library's units (1 = a new letter 12 times a second at 30 fps; a
// cell cannot change more than once a frame, so nothing above about 2.5 looks any faster). 0.875 is
// 3.5 times the 0.25 it started at: two layers change letter about 10 times a second.
constexpr double OVERLAP_SHIMMER = 0.875;

// The shimmer of a cell with this many layers: none for 0 or 1, then OVERLAP_SHIMMER for each more.
double overlap_shimmer(int layers);

class LayerBlend {
public:
    explicit LayerBlend(vtscreen::Picture& picture);

    // Put a layer's letter into a cell: it sets the cell's look if it is the top layer so far, and
    // adds to its shimmer. Each layer adds once to a cell (a second add of the same layer is ignored:
    // the caller picks the one it wants, say the brighter of two roads).
    void add(int row, int col, Layer layer, double brightness, int r, int g, int b);

    int layers_at(int row, int col) const;           // how many layers are in the cell

private:
    vtscreen::Picture& picture_;
    std::vector<uint8_t> present_;                   // per cell: one bit for each layer in it
};

}  // namespace vtw
