// PlaceLabels: which city names to show for a view, and where. A zoomed-out view shows only the
// biggest cities, and each step closer adds smaller ones; the biggest are placed first, and a name
// that would run into one already placed (or off the screen) is left out, so names never overlap.
// Each shows a dot at the place and its name beside it, on the right or, if there is no room, on the
// left. Worked out again only when the view changes.
#pragma once

#include <string>
#include <vector>

#include "basemap.h"
#include "view.h"

namespace vtw {

struct PlaceLabel {
    int col = 0, row = 0;          // the cell of the place itself
    int text_col = 0;              // where the name starts, on the same row
    std::string text;
    uint32_t population = 0;
};

// The smallest city shown at a zoom (km across the screen).
uint32_t min_population_for(double km_across);

constexpr int MAX_NAME_LENGTH = 24;

class PlaceLabels {
public:
    explicit PlaceLabels(const BaseMap& map) : map_(map) {}

    void update(const View& view);
    const std::vector<PlaceLabel>& labels() const { return labels_; }

private:
    const BaseMap& map_;
    std::vector<PlaceLabel> labels_;
    bool have_ = false;
    int cols_ = 0, rows_ = 0;
    GeoPoint centre_;
    double zoom_ = 0, aspect_ = 0, heading_ = 0;
};

}  // namespace vtw
