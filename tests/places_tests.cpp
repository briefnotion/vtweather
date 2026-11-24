// Tests for choosing and placing city names: which cities each zoom shows, names that never
// overlap or leave the screen, the left side when there is no room on the right, and real cities
// on the real map file.
#include <algorithm>
#include <random>
#include <string>

#include "check.h"

#include "../src/place_labels.h"

#ifndef VTW_DATA_DIR
#error "VTW_DATA_DIR must name the data folder"
#endif

namespace {

using vtw::BaseMap;
using vtw::GeoPoint;
using vtw::MapPlace;
using vtw::PlaceLabel;
using vtw::PlaceLabels;

vtw::View make_view(double lat, double lon, double km, int cols = 100, int rows = 40) {
    vtw::View v;
    v.set_cell_aspect(2.0);
    v.set_centre({lat, lon});
    v.set_zoom_km(km);
    v.set_size(cols, rows);
    return v;
}

// a place in the middle of a given cell of a view
MapPlace place_at(const vtw::View& view, int col, int row, uint32_t population, const std::string& name) {
    const GeoPoint p = view.at(col + 0.5, row + 0.5);
    MapPlace m;
    m.lon = (int32_t)std::lround(p.lon * vtw::MAP_UNITS_PER_DEGREE);
    m.lat = (int32_t)std::lround(p.lat * vtw::MAP_UNITS_PER_DEGREE);
    m.population = population;
    m.name = name;
    return m;
}

const PlaceLabel* find(const PlaceLabels& labels, const std::string& name) {
    for (const PlaceLabel& l : labels.labels()) if (l.text == name) return &l;
    return nullptr;
}

// the cells a label takes: its dot and its name
void span(const PlaceLabel& l, int& from, int& to) {
    from = std::min(l.col, l.text_col);
    to = std::max(l.col, l.text_col + (int)l.text.size() - 1);
}

void threshold_tests() {
    // closer views show smaller cities, never fewer
    uint32_t before = 0xFFFFFFFF;
    for (double km = 8000; km >= 1; km /= 1.2) {
        const uint32_t now = vtw::min_population_for(km);
        CHECK(now <= before);
        before = now;
    }
    CHECK(vtw::min_population_for(5000) == 3000000);
    CHECK(vtw::min_population_for(300) == 100000);
    CHECK(vtw::min_population_for(10) == 0);
}

void placing_tests() {
    const vtw::View view = make_view(40, -100, 80);                  // the threshold here is 15 000
    BaseMap map;
    map.places.push_back(place_at(view, 30, 10, 2000000, "Bigtown"));
    map.places.push_back(place_at(view, 60, 20, 30000, "Smalltown"));
    map.places.push_back(place_at(view, 10, 30, 5000, "Tinyville"));         // too small for this zoom
    MapPlace off = place_at(view, 0, 0, 9000000, "Offscreen");
    off.lon += (int32_t)(20 * vtw::MAP_UNITS_PER_DEGREE);                    // far to the east, outside the screen
    map.places.push_back(off);
    PlaceLabels labels(map);
    labels.update(view);

    CHECK(labels.labels().size() == 2);
    const PlaceLabel* big = find(labels, "Bigtown");
    const PlaceLabel* small = find(labels, "Smalltown");
    CHECK(big && small && !find(labels, "Tinyville") && !find(labels, "Offscreen"));
    CHECK(big->col == 30 && big->row == 10 && big->text_col == 32);          // the dot, a blank, then the name
    CHECK(small->col == 60 && small->row == 20 && small->text_col == 62);
    CHECK(labels.labels()[0].text == "Bigtown");                              // the biggest first

    // a place that falls under the name of a bigger one on its row gets no name of its own (not
    // on the right, where it would run into it, and not on the left, where the dot is covered)
    BaseMap crowded;
    crowded.places.push_back(place_at(view, 56, 15, 100000, "Smaller"));
    crowded.places.push_back(place_at(view, 53, 15, 500000, "Larger"));
    PlaceLabels c(crowded);
    c.update(view);
    CHECK(c.labels().size() == 1 && c.labels()[0].text == "Larger");
    // on another row they both fit; and on the same row with room between them, the smaller goes on the left
    BaseMap apart;
    apart.places.push_back(place_at(view, 56, 16, 100000, "Smaller"));
    apart.places.push_back(place_at(view, 53, 15, 500000, "Larger"));
    PlaceLabels a(apart);
    a.update(view);
    CHECK(a.labels().size() == 2);
    BaseMap left;
    left.places.push_back(place_at(view, 50, 15, 100000, "Smaller"));
    left.places.push_back(place_at(view, 53, 15, 500000, "Larger"));
    PlaceLabels lf(left);
    lf.update(view);
    CHECK(lf.labels().size() == 2);
    CHECK(find(lf, "Smaller") && find(lf, "Smaller")->text_col < 50);        // to the left of its dot

    // near the right edge the name goes on the left of the dot
    BaseMap edge;
    edge.places.push_back(place_at(view, 95, 8, 400000, "Edgeville"));
    PlaceLabels e(edge);
    e.update(view);
    CHECK(e.labels().size() == 1);
    CHECK(e.labels()[0].text_col + (int)e.labels()[0].text.size() <= 94);     // ends before the dot, with a blank between
    CHECK(e.labels()[0].text_col >= 1);
    // and when it fits on neither side it is left out
    BaseMap narrow;
    narrow.places.push_back(place_at(make_view(40, -100, 80, 30, 20), 14, 5, 400000, "A very long town name indeed"));
    PlaceLabels n(narrow);
    n.update(make_view(40, -100, 80, 30, 20));
    CHECK(n.labels().empty());

    // names are cut to a sensible length
    BaseMap longname;
    longname.places.push_back(place_at(view, 20, 20, 400000, std::string(60, 'x')));
    PlaceLabels l(longname);
    l.update(view);
    CHECK(l.labels().size() == 1 && (int)l.labels()[0].text.size() == vtw::MAX_NAME_LENGTH);

    // moving the view moves the labels, and the same view gives the same labels
    vtw::View moved = view;
    moved.pan_cells(10, 0);
    labels.update(moved);
    CHECK(find(labels, "Bigtown") && find(labels, "Bigtown")->col == 20);
    labels.update(moved);
    CHECK(labels.labels().size() == 2);
}

void real_tests() {
    BaseMap map;
    std::string error;
    CHECK(vtw::load_basemap(std::string(VTW_DATA_DIR) + "/basemap.bin", map, error));
    CHECK(map.places.size() > 2000);

    struct Case { const char* name; double lat, lon, km; };
    const Case cases[] = {
        {"Seattle", 47.6, -122.3, 100}, {"Wichita", 37.69, -97.34, 200}, {"Chicago", 41.9, -87.6, 1800},
        {"Denver", 39.74, -105.0, 600}, {"Houston", 29.76, -95.37, 300}, {"Miami", 25.77, -80.19, 150},
        {"Chicago", 39.0, -97.0, 4500}, {"Los Angeles", 34.05, -118.25, 400},
    };
    for (const Case& c : cases) {
        PlaceLabels labels(map);
        labels.update(make_view(c.lat, c.lon, c.km, 120, 40));
        if (!find(labels, c.name)) std::printf("  %s not shown at %.0f km\n", c.name, c.km);
        CHECK(find(labels, c.name) != nullptr);
    }
    // a world view shows only the biggest cities
    {
        PlaceLabels labels(map);
        labels.update(make_view(39, -97, 6000, 120, 40));
        CHECK(labels.labels().size() >= 3);
        for (const PlaceLabel& l : labels.labels()) CHECK(l.population >= 3000000);
    }
    // in any view: inside the screen, names apart, biggest first, no more than the limit, plain ASCII
    std::mt19937 rng(2026);
    int shown = 0;
    for (int round = 0; round < 80; round++) {
        const double lat = 25 + (rng() % 2500) / 100.0, lon = -125 + (rng() % 6500) / 100.0;
        const double km = std::exp(std::log(5.0) + (std::log(6000.0) - std::log(5.0)) * (rng() % 1000) / 1000.0);
        const int cols = 70 + (int)(rng() % 140), rows = 25 + (int)(rng() % 40);
        vtw::View view = make_view(lat, lon, km, cols, rows);
        if (rng() % 4 == 0) view.set_heading((double)(rng() % 360));
        PlaceLabels labels(map);
        labels.update(view);
        const std::vector<PlaceLabel>& list = labels.labels();
        CHECK(list.size() <= std::max<size_t>(5, (size_t)cols * rows / 150));
        shown += (int)list.size();
        for (size_t i = 0; i < list.size(); i++) {
            const PlaceLabel& l = list[i];
            int from, to;
            span(l, from, to);
            CHECK(l.row >= 0 && l.row < rows && l.col >= 0 && l.col < cols && from >= 1 && to <= cols - 2);
            CHECK(l.population >= vtw::min_population_for(km));
            if (i > 0) CHECK(l.population <= list[i - 1].population);
            for (char ch : l.text) CHECK(ch >= 32 && ch < 127);
            for (size_t k = i + 1; k < list.size(); k++) {
                if (list[k].row != l.row) continue;
                int f2, t2;
                span(list[k], f2, t2);
                CHECK(t2 + 1 < from || f2 - 1 > to);                 // at least one blank cell between
            }
        }
    }
    std::printf("  %d names shown in 80 random views\n", shown);
    CHECK(shown > 100);
}

}  // namespace

int main() {
    threshold_tests();
    placing_tests();
    real_tests();
    if (failures == 0) std::printf("all place label tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
