#include "layers.h"

#include <algorithm>

namespace vtw {

namespace {

int count_bits(uint8_t v) {
    int n = 0;
    for (; v; v >>= 1) n += v & 1;
    return n;
}

}  // namespace

double overlap_shimmer(int layers) { return layers <= 1 ? 0.0 : (layers - 1) * OVERLAP_SHIMMER; }

LayerBlend::LayerBlend(vtscreen::Picture& picture)
    : picture_(picture), present_((size_t)picture.rows * picture.cols, 0) {}

int LayerBlend::layers_at(int row, int col) const { return count_bits(present_[(size_t)row * picture_.cols + col]); }

void LayerBlend::add(int row, int col, Layer layer, double brightness, int r, int g, int b) {
    uint8_t& present = present_[(size_t)row * picture_.cols + col];
    const uint8_t bit = (uint8_t)(1u << (int)layer);
    if (present & bit) return;
    vtscreen::PicCell& cell = picture_.at(row, col);
    if (present == 0) {                                     // the first: takes the cell's letter (not its background)
        cell.lum = brightness;
        cell.r = r; cell.g = g; cell.b = b;
    } else {                                                // the next: the brighter, and the colours added
        cell.lum = std::max(cell.lum, brightness);
        cell.r = std::min(255.0, cell.r + r);
        cell.g = std::min(255.0, cell.g + g);
        cell.b = std::min(255.0, cell.b + b);
    }
    present |= bit;
    cell.shimmer = overlap_shimmer(count_bits(present));
}

}  // namespace vtw
