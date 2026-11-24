// Tests for the cell grid and for drawing the rain.
#include <algorithm>
#include <string>

#include "check.h"

#include "../src/cell_grid.h"
#include "../src/draw_radar.h"

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
    // stronger rain is brighter, always above the map's lines, and shimmers faster
    double before_brightness = 0, before_shimmer = 0;
    for (int dbz = 5; dbz <= 75; dbz += 1) {
        const vtw::RainLook look = vtw::rain_look(dbz);
        CHECK(look.brightness >= before_brightness);
        CHECK(look.brightness > 0.30 && look.brightness <= 1.0);
        CHECK(look.shimmer >= before_shimmer && look.shimmer <= 2.5);
        CHECK(look.r >= 0 && look.r <= 255 && look.g >= 0 && look.g <= 255 && look.b >= 0 && look.b <= 255);
        before_brightness = look.brightness;
        before_shimmer = look.shimmer;
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
    vtw::draw_radar(grid, frame, picture);

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
            else CHECK_NEAR(c.lum, vtw::rain_look(dbz).brightness, 1e-12);
        }
    CHECK(lit > 0);
    CHECK(light > 0 && heavy > light && core > heavy);        // brighter with strength, and the core shows
    // the bar row is not touched
    for (int col = 0; col < 50; col++) CHECK(picture.at(20, col).lum == 0);

    // no picture at all: nothing is drawn
    vtscreen::Picture empty;
    empty.init(20, 50);
    vtw::draw_radar(grid, vtw::RadarFrame(), empty);
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
        CHECK(!c.text && c.lum >= before && c.lum > 0.3);
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

}  // namespace

int main() {
    grid_tests();
    look_tests();
    draw_tests();
    legend_tests();
    if (failures == 0) std::printf("all drawing tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
