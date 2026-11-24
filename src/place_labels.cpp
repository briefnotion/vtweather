#include "place_labels.h"

#include <algorithm>

namespace vtw {

uint32_t min_population_for(double km_across) {
    if (km_across >= 4000) return 3000000;
    if (km_across >= 2000) return 1500000;
    if (km_across >= 1000) return 600000;
    if (km_across >= 500) return 250000;
    if (km_across >= 250) return 100000;
    if (km_across >= 120) return 40000;
    if (km_across >= 60) return 15000;
    return 0;
}

void PlaceLabels::update(const View& view) {
    if (have_ && cols_ == view.cols() && rows_ == view.rows() && zoom_ == view.zoom_km() &&
        aspect_ == view.cell_aspect() && heading_ == view.heading() && centre_.lat == view.centre().lat &&
        centre_.lon == view.centre().lon)
        return;
    have_ = true;
    cols_ = view.cols(); rows_ = view.rows();
    zoom_ = view.zoom_km(); aspect_ = view.cell_aspect(); heading_ = view.heading(); centre_ = view.centre();
    labels_.clear();

    // the places that are big enough and on the screen, the biggest first
    const uint32_t threshold = min_population_for(zoom_);
    struct Candidate { const MapPlace* place; int col, row; };
    std::vector<Candidate> candidates;
    for (const MapPlace& place : map_.places) {
        if (place.population < threshold) continue;
        double col, row;
        view.to_cell(GeoPoint{place.lat / MAP_UNITS_PER_DEGREE, place.lon / MAP_UNITS_PER_DEGREE}, col, row);
        if (col < 0 || row < 0 || col >= cols_ || row >= rows_) continue;
        candidates.push_back({&place, (int)col, (int)row});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.place->population > b.place->population; });

    // place them: the dot, then the name to its right (or left), a blank cell around each
    std::vector<unsigned char> taken((size_t)cols_ * rows_, 0);
    auto free_span = [&](int row, int from, int to) {          // cells from..to (inclusive), with a blank on each side
        if (from < 1 || to > cols_ - 2) return false;
        for (int c = from - 1; c <= to + 1; c++)
            if (taken[(size_t)row * cols_ + c]) return false;
        return true;
    };
    const size_t most = std::max<size_t>(5, (size_t)cols_ * rows_ / 150);
    for (const Candidate& c : candidates) {
        if (labels_.size() >= most) break;
        std::string name = c.place->name.substr(0, MAX_NAME_LENGTH);
        const int length = (int)name.size();
        int text_col;
        if (free_span(c.row, c.col, c.col + 1 + length)) text_col = c.col + 2;            // dot, a space, the name
        else if (free_span(c.row, c.col - 1 - length, c.col)) text_col = c.col - 1 - length;
        else continue;
        const int from = std::min(c.col, text_col), to = std::max(c.col, text_col + length - 1);
        for (int col = from; col <= to; col++) taken[(size_t)c.row * cols_ + col] = 1;
        PlaceLabel label;
        label.col = c.col; label.row = c.row; label.text_col = text_col;
        label.text = std::move(name);
        label.population = c.place->population;
        labels_.push_back(std::move(label));
    }
}

}  // namespace vtw
