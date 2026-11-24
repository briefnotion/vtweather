// Tests for the cell grid and for drawing the rain.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "check.h"

#include "../src/cell_grid.h"
#include "../src/draw_ground.h"
#include "../src/draw_radar.h"
#include "../src/layers.h"

namespace {

using vtw::GeoBox;
using vtw::GeoPoint;

void grid_tests() {
    vtw::View view;
    view.set_cell_aspect(2.0);
    view.set_centre({37.69, -97.34});
    view.set_zoom_km(80);
    view.set_size(60, 20);
    const vtw::CellGrid grid(view);
    CHECK(grid.cols() == 60 && grid.rows() == 20);
    // a corner is where the View says; a cell holds the point at its middle and touches its neighbour
    CHECK_NEAR(grid.corner(0, 0).lat, view.at(0, 0).lat, 1e-12);
    CHECK_NEAR(grid.corner(60, 20).lon, view.at(60, 20).lon, 1e-12);
    for (int col : {0, 10, 59})
        for (int row : {0, 7, 19}) {
            const GeoBox b = grid.box(col, row);
            const GeoPoint middle = view.at(col + 0.5, row + 0.5);
            CHECK(middle.lat > b.south && middle.lat < b.north && middle.lon > b.west && middle.lon < b.east);
            if (col < 59) CHECK_NEAR(grid.box(col + 1, row).west, b.east, 1e-9);
            if (row < 19) CHECK_NEAR(grid.box(col, row + 1).north, b.south, 1e-9);
        }
    // north is up: higher rows are further south
    CHECK(grid.box(5, 0).north > grid.box(5, 19).north);
}

void look_tests() {
    // stronger rain is brighter, always above the map's lines
    double before_brightness = 0;
    for (int dbz = 5; dbz <= 75; dbz += 1) {
        const vtw::RainLook look = vtw::rain_look(dbz);
        CHECK(look.brightness >= before_brightness);
        CHECK(look.brightness > 0.30 && look.brightness <= 1.0);
        CHECK(look.r >= 0 && look.r <= 255 && look.g >= 0 && look.g <= 255 && look.b >= 0 && look.b <= 255);
        before_brightness = look.brightness;
    }
    CHECK(vtw::rain_look(5).brightness < 0.4);
    CHECK(vtw::rain_look(60).brightness > 0.95);
    // the colours follow the weather scale: green for moderate, yellow for heavy, red for very heavy
    const vtw::RainLook green = vtw::rain_look(25), yellow = vtw::rain_look(35), red = vtw::rain_look(50);
    CHECK(green.g > green.r && green.g > green.b);
    CHECK(yellow.r > 200 && yellow.g > 200 && yellow.b < 100);
    CHECK(red.r > 200 && red.g < 80 && red.b < 80);
}

void draw_tests() {
    vtw::View view;
    view.set_cell_aspect(2.0);
    view.set_centre({10.0, 20.0});
    view.set_zoom_km(100);
    view.set_size(50, 20);
    const vtw::CellGrid grid(view);

    // a made-up picture over the whole screen, with a light patch and a strong patch
    vtw::RadarFrame frame;
    frame.box = view.box(0.1);
    frame.width = 200; frame.height = 100;
    frame.dbz.assign((size_t)frame.width * frame.height, 0);
    auto fill = [&](int x0, int y0, int x1, int y1, uint8_t v) {
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) frame.dbz[(size_t)y * frame.width + x] = v;
    };
    fill(20, 20, 60, 40, 12);      // light rain at the upper left
    fill(120, 60, 150, 80, 48);    // heavy rain lower right
    fill(130, 65, 134, 69, 62);    // a small core inside it, smaller than a cell

    vtscreen::Picture picture;
    picture.init(20, 50);
    vtw::LayerBlend blend(picture);
    vtw::draw_radar(grid, frame, blend);

    // find where the light and heavy patches landed on the screen
    double light = -1, heavy = -1, core = -1;
    int lit = 0;
    for (int row = 0; row < 20; row++)
        for (int col = 0; col < 50; col++) {
            const vtscreen::PicCell& c = picture.at(row, col);
            if (c.lum > 0) lit++;
            const uint8_t dbz = frame.strongest_in(grid.box(col, row));
            if (dbz == 12) light = c.lum;
            if (dbz == 48) heavy = c.lum;
            if (dbz == 62) core = c.lum;
            // a cell with no rain under it is left alone
            if (dbz == 0) CHECK(c.lum == 0);
            else { CHECK_NEAR(c.lum, vtw::rain_look(dbz).brightness, 1e-12); CHECK(c.shimmer == 0); }          // alone, it holds still
        }
    CHECK(lit > 0);
    CHECK(light > 0 && heavy > light && core > heavy);        // brighter with strength, and the core shows
    // the bar row is not touched
    for (int col = 0; col < 50; col++) CHECK(picture.at(20, col).lum == 0);

    // no picture at all: nothing is drawn
    vtscreen::Picture empty;
    empty.init(20, 50);
    vtw::LayerBlend empty_blend(empty);
    vtw::draw_radar(grid, vtw::RadarFrame(), empty_blend);
    for (const vtscreen::PicCell& c : empty.cell) CHECK(c.lum == 0);
}

void legend_tests() {
    vtscreen::Picture picture;
    picture.init(30, 100);
    vtw::draw_legend(picture);
    const int strip = picture.bar_row() - 1;
    const int left = picture.cols - vtw::LEGEND_WIDTH - 1;
    // a strip of rain cells from light to heavy, brighter along it, as the rain itself is drawn
    double before = 0;
    for (int i = 0; i < vtw::LEGEND_WIDTH; i++) {
        const vtscreen::PicCell& c = picture.at(strip, left + i);
        CHECK(!c.text && c.lum >= before && c.lum > 0.3 && c.shimmer == 0);
        before = c.lum;
    }
    CHECK_NEAR(picture.at(strip, left).lum, vtw::rain_look(5).brightness, 1e-12);
    CHECK_NEAR(picture.at(strip, left + vtw::LEGEND_WIDTH - 1).lum, vtw::rain_look(60).brightness, 1e-12);
    CHECK(picture.at(strip, left + 14).g != picture.at(strip, left).g || picture.at(strip, left + 14).r != picture.at(strip, left).r);   // the colour changes
    // the words above, and the right edge and the status bar left alone
    std::string words;
    for (int c = 0; c < picture.cols; c++) words += picture.at(strip - 1, c).text ? (char)picture.at(strip - 1, c).ch : ' ';
    CHECK(words.find("light rain") != std::string::npos && words.find("heavy") != std::string::npos);
    CHECK(words.find("light rain") < words.find("heavy"));
    CHECK(!picture.at(strip, picture.cols - 1).lum && picture.at(strip, picture.cols - 1).lum == 0);
    for (int c = 0; c < picture.cols; c++) CHECK(!picture.at(picture.bar_row(), c).text && picture.at(picture.bar_row(), c).lum == 0);
    // too small a screen: nothing
    for (const std::pair<int, int>& size : {std::pair<int, int>{69, 30}, {100, 15}, {40, 20}}) {
        vtscreen::Picture small;
        small.init(size.second, size.first);
        vtw::draw_legend(small);
        for (const vtscreen::PicCell& c : small.cell) CHECK(!c.text && c.lum == 0);
    }
}

// ---- the ground: water, land, and the lines on the land

vtw::MapPolygon ground_square(double south, double west, double north, double east) {
    auto u = [](double d) { return (int32_t)std::lround(d * vtw::MAP_UNITS_PER_DEGREE); };
    vtw::MapPolygon p;
    p.rings.push_back({u(west), u(south), u(east), u(south), u(east), u(north), u(west), u(north)});
    p.box = {south, west, north, east};
    return p;
}

vtw::MapLine ground_line(double lon0, double lat0, double lon1, double lat1) {
    auto u = [](double d) { return (int32_t)std::lround(d * vtw::MAP_UNITS_PER_DEGREE); };
    vtw::MapLine l;
    l.points = {u(lon0), u(lat0), u(lon1), u(lat1)};
    l.box = {std::min(lat0, lat1), std::min(lon0, lon1), std::max(lat0, lat1), std::max(lon0, lon1)};
    return l;
}

// the layers a cell has, worked out here from its features, as an independent check of the drawing: how many,
// and the look of the top one (borders, then the coast, then roads, then rivers)
struct Expected { int layers = 0; double lum = 0; double rgb[3] = {0, 0, 0}; };

Expected expected_for(vtw::Ground kind, uint8_t f) {
    Expected e;
    auto layer = [&](bool present, double lum, int r, int g, int b) {
        if (!present) return;
        if (e.layers == 0) { e.lum = lum; e.rgb[0] = r; e.rgb[1] = g; e.rgb[2] = b; }   // asked in order of priority: the first is the top
        e.layers++;
    };
    layer(f & vtw::FEATURE_COUNTRY, 0.26, 215, 120, 150);
    layer(!(f & vtw::FEATURE_COUNTRY) && (f & vtw::FEATURE_STATE), 0.20, 150, 120, 175);
    layer(kind == vtw::Ground::Coast, 0.33, 190, 170, 120);
    layer(f & vtw::FEATURE_HIGHWAY, 0.23, 230, 180, 85);
    layer(f & vtw::FEATURE_RIVER, 0.15, 70, 140, 205);
    // a country border and a state border are one layer: the loop above counted a state border only when there was no country border
    return e;
}

void ground_tests() {
    // an island with lines across it, in a sea: along latitude 1.2 a river, a border and a highway run together
    // (three layers in one cell); the island's north edge is also the coast, with a river, a border and a highway
    // on it (four); a country border, a river that runs along part of it (two layers) and more that cross the others
    vtw::BaseMap map;
    const vtw::GeoBox world{-90, -180, 90, 180};
    auto polygon_layer = [&](vtw::LayerKind kind, std::vector<vtw::MapPolygon> polygons) {
        vtw::MapLayer l; l.kind = kind; l.detail = 2; l.coverage = world; l.polygons = std::move(polygons); return l;
    };
    auto lines_layer = [&](vtw::LayerKind kind, std::vector<vtw::MapLine> lines) {
        vtw::MapLayer l; l.kind = kind; l.detail = 2; l.coverage = world; l.lines = std::move(lines); return l;
    };
    map.layers.push_back(polygon_layer(vtw::LayerKind::Land, {ground_square(-2, -2, 2, 2)}));
    map.layers.push_back(lines_layer(vtw::LayerKind::River, {ground_line(-1.5, 1.2, 1.5, 1.2), ground_line(-1.5, 1.95, 1.5, 1.95), ground_line(0.8, -1.5, 0.8, 0.5), ground_line(-1.0, -0.6, 0.2, -0.6)}));
    map.layers.push_back(lines_layer(vtw::LayerKind::State, {ground_line(-1.5, 1.2, 1.5, 1.2), ground_line(-1.5, 1.95, 1.5, 1.95), ground_line(-0.7, -1.5, -0.7, 1.5)}));
    map.layers.push_back(lines_layer(vtw::LayerKind::Country, {ground_line(-1.5, -0.6, 1.5, -0.6)}));
    map.layers.push_back(lines_layer(vtw::LayerKind::Highway, {ground_line(-1.5, 1.2, 1.5, 1.2), ground_line(-1.5, 1.95, 1.5, 1.95)}));
    vtw::View view;
    view.set_cell_aspect(2.0);
    view.set_centre({0, 0});
    view.set_zoom_km(700);
    view.set_size(120, 50);
    vtw::GroundMap ground(map);
    ground.update(view);
    const int rows = 50, cols = 120;
    vtscreen::Picture picture;
    picture.init(rows, cols);
    vtw::LayerBlend blend(picture);
    vtw::draw_ground(ground, picture, blend);

    int water = 0, plain_land = 0, by_layers[8] = {0};
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++) {
            const vtscreen::PicCell& c = picture.at(row, col);
            const vtw::Ground kind = ground.at(col, row);
            const uint8_t f = ground.features_at(col, row);
            if (kind == vtw::Ground::Water) {
                water++;
                // navy letters on a navy background: the same colour, so in colour the letters cannot be seen
                CHECK(c.r == 8 && c.g == 22 && c.b == 48);
                CHECK(c.alt_lum == 1.0 && c.alt_r == c.r && c.alt_g == c.g && c.alt_b == c.b);
                // just above the first of the 16 brightness steps, so that without colour there are letters,
                // and a shimmer that is not zero but as low as it gets
                CHECK(c.lum >= 1.0 / 16 && c.lum < 2.0 / 16);
                CHECK(c.shimmer > 0 && c.shimmer <= 0.002);
                CHECK(!c.text && blend.layers_at(row, col) == 0);
                continue;
            }
            const Expected e = expected_for(kind, f);
            CHECK(blend.layers_at(row, col) == e.layers);
            by_layers[std::min(e.layers, 7)]++;
            if (e.layers == 0) {
                plain_land++;                                       // land: no colour and no letters, untouched
                CHECK(c.lum == 0 && c.alt_lum == 0 && c.r == 0 && c.g == 0 && c.b == 0 && c.shimmer == 1 && !c.text);
                continue;
            }
            // a line, or several: letters only, no colour behind them; the look of the top one, not mixed, and a
            // shimmer of none for one layer, the base for two, twice it for three, three times it for four
            CHECK(c.alt_lum == 0);
            CHECK_NEAR(c.lum, e.lum, 1e-12);
            CHECK_NEAR(c.r, e.rgb[0], 1e-9);
            CHECK_NEAR(c.g, e.rgb[1], 1e-9);
            CHECK_NEAR(c.b, e.rgb[2], 1e-9);
            CHECK_NEAR(c.shimmer, (e.layers - 1) * vtw::OVERLAP_SHIMMER, 1e-12);
        }
    CHECK(water > 500 && plain_land > 500);
    CHECK(by_layers[1] > 20 && by_layers[2] > 3 && by_layers[3] > 10 && by_layers[4] > 5);          // one, two, three and four layers

    // how it looks as letters. Without colour the water is faint dots and the land is empty (the other way
    // round from how it was); with colour the water is one flat navy
    auto lines_of = [&](const std::string& text) {
        std::vector<std::string> lines;
        for (size_t at = 0; at < text.size();) {
            const size_t end = text.find('\n', at);
            lines.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        return lines;
    };
    auto render = [&](double t) { return lines_of(vtscreen::render_plain(picture, rows, cols, t, 30, false, false, false)); };
    const std::vector<std::string> now = render(0.0);
    CHECK((int)now.size() == rows);
    int dotted_water = 0, blank_land = 0, stray = 0;
    for (int row = 0; row < rows && row < (int)now.size(); row++)
        for (int col = 0; col < cols && col < (int)now[row].size(); col++) {
            const char ch = now[row][col];
            const vtw::Ground kind = ground.at(col, row);
            if (kind == vtw::Ground::Water) {
                if (ch == '.' || ch == '`' || ch == '\'') dotted_water++;
                else stray++;
            } else if (ground.features_at(col, row) == 0 && kind == vtw::Ground::Land) {
                if (ch == ' ') blank_land++;
                else stray++;
            }
        }
    CHECK(stray == 0 && dotted_water == water && blank_land == plain_land);

    // overlaps show without colour, through the shimmer: a cell with one layer never changes its letter, and
    // most cells with several do
    const std::vector<std::string> later = render(0.5), much_later = render(300.0);
    int single_changed = 0, single_cells = 0, overlap_changed = 0, overlap_cells = 0;
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++) {
            if (ground.at(col, row) == vtw::Ground::Water) continue;
            const int layers = blend.layers_at(row, col);
            if (layers == 1) {
                single_cells++;
                single_changed += (now[row][col] != later[row][col]) + (now[row][col] != much_later[row][col]);
            } else if (layers >= 2) {
                overlap_cells++;
                overlap_changed += now[row][col] != later[row][col];
            }
        }
    CHECK(single_cells > 20 && single_changed == 0);
    CHECK(overlap_cells > 20 && overlap_changed > overlap_cells * 7 / 10);
    // the water hardly moves: the same ten seconds later, but it does move over a few minutes
    const std::vector<std::string> ten = render(10.0);
    int water_changed_in_ten = 0, water_changed_in_400 = 0;
    const std::vector<std::string> four_hundred = render(400.0);
    for (int row = 0; row < rows; row++)
        for (int col = 0; col < cols; col++)
            if (ground.at(col, row) == vtw::Ground::Water) {
                water_changed_in_ten += now[row][col] != ten[row][col];
                water_changed_in_400 += now[row][col] != four_hundred[row][col];
            }
    CHECK(water_changed_in_ten == 0 && water_changed_in_400 > water / 10);

    // with colour, the letter colour and the background colour sent for water are the same
    const std::string coloured = vtscreen::render_plain(picture, rows, cols, 0.0, 30, true, false, false);
    CHECK(coloured.find("38;2;0;17;51") != std::string::npos && coloured.find("48;2;0;17;51") != std::string::npos);
}

// ---- LayerBlend on its own

void blend_tests() {
    using vtw::Layer;
    CHECK(vtw::overlap_shimmer(0) == 0 && vtw::overlap_shimmer(1) == 0);
    CHECK_NEAR(vtw::overlap_shimmer(2), vtw::OVERLAP_SHIMMER, 1e-12);
    CHECK_NEAR(vtw::overlap_shimmer(4), 3 * vtw::OVERLAP_SHIMMER, 1e-12);
    CHECK(vtw::OVERLAP_SHIMMER >= 0.75 && vtw::OVERLAP_SHIMMER <= 1.0);          // three to four times the first 0.25

    vtscreen::Picture picture;
    picture.init(4, 6);
    vtw::LayerBlend blend(picture);
    // a cell with the water's own look: the first layer takes its letter and shimmer, not its background
    vtscreen::PicCell& water = picture.at(1, 1);
    water.lum = 0.07; water.shimmer = 0.001; water.r = 8; water.g = 22; water.b = 48;
    water.alt_lum = 1; water.alt_r = 8; water.alt_g = 22; water.alt_b = 48;
    blend.add(1, 1, Layer::Road, 0.2, 100, 50, 25);
    CHECK(blend.layers_at(1, 1) == 1);
    CHECK(water.lum == 0.2 && water.r == 100 && water.g == 50 && water.b == 25 && water.shimmer == 0);
    CHECK(water.alt_lum == 1 && water.alt_b == 48);
    // a second layer that is higher takes the look (the colours are not mixed); the shimmer is the base
    blend.add(1, 1, Layer::Rain, 0.6, 200, 10, 5);
    CHECK(blend.layers_at(1, 1) == 2);
    CHECK(water.lum == 0.6 && water.r == 200 && water.g == 10 && water.b == 5);
    CHECK_NEAR(water.shimmer, vtw::OVERLAP_SHIMMER, 1e-12);
    // lower layers, however bright, leave the look alone; each one adds the base to the shimmer
    blend.add(1, 1, Layer::River, 0.9, 1, 2, 3);
    CHECK(water.lum == 0.6 && water.r == 200 && blend.layers_at(1, 1) == 3 && std::fabs(water.shimmer - 2 * vtw::OVERLAP_SHIMMER) < 1e-12);
    blend.add(1, 1, Layer::Grid, 0.95, 4, 5, 6);
    CHECK(water.lum == 0.6 && water.g == 10 && std::fabs(water.shimmer - 3 * vtw::OVERLAP_SHIMMER) < 1e-12);
    // the same layer again changes nothing
    blend.add(1, 1, Layer::Rain, 1.0, 255, 255, 255);
    CHECK(blend.layers_at(1, 1) == 4 && water.lum == 0.6 && water.g == 10);
    // other cells are not touched
    CHECK(blend.layers_at(1, 2) == 0 && picture.at(1, 2).lum == 0 && picture.at(1, 2).shimmer == 1);

    // the priority: rain over the grid over borders over the coast over roads over rivers; whichever order
    // two layers are added in, the higher one decides the look
    const Layer by_priority[6] = {Layer::River, Layer::Road, Layer::Coast, Layer::Border, Layer::Grid, Layer::Rain};
    for (int low = 0; low < 6; low++)
        for (int high = low + 1; high < 6; high++)
            for (int order = 0; order < 2; order++) {
                vtscreen::Picture p;
                p.init(1, 1);
                vtw::LayerBlend b(p);
                const double lum_low = 0.9, lum_high = 0.1;                              // the higher layer is the dimmer
                if (order == 0) { b.add(0, 0, by_priority[low], lum_low, 10, 20, 30); b.add(0, 0, by_priority[high], lum_high, 40, 50, 60); }
                else { b.add(0, 0, by_priority[high], lum_high, 40, 50, 60); b.add(0, 0, by_priority[low], lum_low, 10, 20, 30); }
                CHECK(p.at(0, 0).lum == lum_high && p.at(0, 0).r == 40 && p.at(0, 0).g == 50 && p.at(0, 0).b == 60);
                CHECK_NEAR(p.at(0, 0).shimmer, vtw::OVERLAP_SHIMMER, 1e-12);
            }

    // every order of four layers gives the same cell: the rain's look, three times the base
    const Layer all[4] = {Layer::Grid, Layer::Border, Layer::Coast, Layer::Rain};
    const double lum[4] = {0.18, 0.26, 0.33, 0.55};
    const int red[4] = {90, 215, 190, 120}, green[4] = {135, 120, 170, 200}, blue[4] = {175, 150, 120, 60};
    std::vector<int> order = {0, 1, 2, 3};
    do {
        vtscreen::Picture p;
        p.init(1, 1);
        vtw::LayerBlend b(p);
        for (int i : order) b.add(0, 0, all[i], lum[i], red[i], green[i], blue[i]);
        const vtscreen::PicCell& c = p.at(0, 0);
        CHECK(c.lum == 0.55 && c.r == 120 && c.g == 200 && c.b == 60);
        CHECK_NEAR(c.shimmer, 3 * vtw::OVERLAP_SHIMMER, 1e-12);
    } while (std::next_permutation(order.begin(), order.end()));

    // all six layers in one cell: five times the base, and the rain's look
    vtscreen::Picture six;
    six.init(1, 1);
    vtw::LayerBlend six_blend(six);
    for (int i = 0; i < vtw::LAYER_COUNT; i++) six_blend.add(0, 0, (Layer)i, 0.2 + 0.1 * i, 10 + i, 10, 10);
    CHECK(six_blend.layers_at(0, 0) == 6 && std::fabs(six.at(0, 0).shimmer - 5 * vtw::OVERLAP_SHIMMER) < 1e-12);
    CHECK(six.at(0, 0).r == 15 && std::fabs(six.at(0, 0).lum - 0.7) < 1e-12);             // the sixth, Rain, is the top
}

}  // namespace

int main() {
    grid_tests();
    look_tests();
    draw_tests();
    legend_tests();
    ground_tests();
    blend_tests();
    if (failures == 0) std::printf("all drawing tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
