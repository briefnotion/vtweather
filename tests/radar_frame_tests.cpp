// Tests for turning radar colours into strengths, and for looking up a frame by place.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"

#include "../src/radar_frame.h"

#ifndef VTW_TEST_DATA_DIR
#error "VTW_TEST_DATA_DIR must name the tests/data folder"
#endif

namespace {

using vtw::GeoBox;
using vtw::GeoPoint;
using vtw::dbz_from_colour;

vtw::RadarFrame load_frame(const char* name, GeoBox box) {
    std::ifstream in(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png", std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    vtw::Image image;
    std::string error;
    vtw::RadarFrame frame;
    CHECK(vtw::decode_png(bytes.str(), image, error));
    CHECK(vtw::make_radar_frame(image, box, frame, error));
    return frame;
}

void colour_tests() {
    // colours on the scale give their values
    CHECK(dbz_from_colour(210, 212, 180, 255) == 5);        // the faintest
    CHECK(dbz_from_colour(14, 214, 20, 255) == 22);         // light green
    CHECK(dbz_from_colour(13, 198, 19, 255) == 25);
    CHECK(dbz_from_colour(9, 94, 9, 255) == 31);            // dark green
    CHECK(dbz_from_colour(255, 211, 0, 255) == 35);         // yellow
    CHECK(dbz_from_colour(255, 0, 0, 255) == 50);           // red
    CHECK(dbz_from_colour(255, 255, 255, 255) == 75);       // white

    // the scale only rises, and each of its colours gives its own value; walk along it
    const std::vector<vtw::ScaleStop>& scale = vtw::colour_scale();
    CHECK(scale.size() >= 20);
    int before = 0;
    for (size_t i = 0; i + 1 < scale.size(); i++) {
        CHECK(scale[i + 1].dbz > scale[i].dbz);
        CHECK(dbz_from_colour((uint8_t)scale[i].r, (uint8_t)scale[i].g, (uint8_t)scale[i].b, 255) == std::lround(scale[i].dbz));
        for (int step = 0; step < 10; step++) {
            const double t = step / 10.0;
            const int v = dbz_from_colour((uint8_t)std::lround(scale[i].r + t * (scale[i + 1].r - scale[i].r)),
                                          (uint8_t)std::lround(scale[i].g + t * (scale[i + 1].g - scale[i].g)),
                                          (uint8_t)std::lround(scale[i].b + t * (scale[i + 1].b - scale[i].b)), 255);
            CHECK(v >= before - 1);                        // never falls (a rounding step aside)
            CHECK(v >= scale[i].dbz - 1 && v <= scale[i + 1].dbz + 1);
            before = std::max(before, v);
        }
    }
    CHECK(before >= 70);

    // transparent and mostly transparent pixels are no rain; so is a colour that is nowhere near
    CHECK(dbz_from_colour(14, 214, 20, 0) == 0);
    CHECK(dbz_from_colour(14, 214, 20, 100) == 0);
    CHECK(dbz_from_colour(14, 214, 20, 200) == 22);
    CHECK(dbz_from_colour(156, 152, 93, 255) == 0);         // the blur between pale and yellow
    CHECK(dbz_from_colour(90, 90, 60, 255) == 0);
    CHECK(dbz_from_colour(0, 0, 0, 255) == 0);

    // strong colours that are not on the list are still placed, by hue
    CHECK(dbz_from_colour(230, 0, 0, 255) >= 50);
    CHECK(dbz_from_colour(140, 0, 0, 255) >= 50);
    CHECK(dbz_from_colour(240, 40, 230, 255) >= 60);
    CHECK(dbz_from_colour(120, 0, 160, 255) >= 65);
    CHECK(dbz_from_colour(250, 160, 0, 255) >= 40);
}

void frame_tests() {
    // a real picture of rain over the Seattle area (see tests/data)
    const GeoBox seattle{47.0, -123.4, 48.2, -121.6};
    const vtw::RadarFrame frame = load_frame("noaa_seattle_rain", seattle);
    CHECK(frame.width == 270 && frame.height == 180);
    int rain = 0, strongest = 0;
    for (uint8_t v : frame.dbz) { rain += v > 0; strongest = std::max<int>(strongest, v); }
    CHECK(rain == 47135);                                   // every coloured pixel was understood
    CHECK(strongest >= 28 && strongest <= 33);              // moderate rain, nothing stronger
    int light = 0, moderate = 0;
    for (uint8_t v : frame.dbz) { light += (v > 0 && v < 20); moderate += (v >= 20); }
    CHECK(light > 10000 && moderate > 15000);

    // a picture with almost no rain: a few faint echoes
    const vtw::RadarFrame clear = load_frame("noaa_kansas_clear", GeoBox{36.0, -99.0, 39.0, -95.0});
    int echoes = 0, loudest = 0;
    for (uint8_t v : clear.dbz) { echoes += v > 0; loudest = std::max<int>(loudest, v); }
    CHECK(echoes == 270 && loudest <= 12);

    // looking up by place: the pixel under it
    const double dx = (seattle.east - seattle.west) / frame.width, dy = (seattle.north - seattle.south) / frame.height;
    for (int x : {0, 1, 100, 269})
        for (int y : {0, 1, 90, 179}) {
            const GeoPoint centre{seattle.north - (y + 0.5) * dy, seattle.west + (x + 0.5) * dx};
            CHECK(frame.value_at(centre) == frame.at(x, y));
        }
    CHECK(frame.value_at({50.0, -122.5}) == 0);             // north of the picture
    CHECK(frame.value_at({47.5, -130.0}) == 0);             // west of it

    // an area takes the strongest pixel under it
    {
        const int x = 120, y = 80;
        GeoBox two_by_two{seattle.north - (y + 2) * dy + dy * 0.1, seattle.west + x * dx + dx * 0.1,
                          seattle.north - y * dy - dy * 0.1, seattle.west + (x + 2) * dx - dx * 0.1};
        const int most = std::max({frame.at(x, y), frame.at(x + 1, y), frame.at(x, y + 1), frame.at(x + 1, y + 1)});
        CHECK(frame.strongest_in(two_by_two) == most);
        CHECK(frame.strongest_in(seattle) == strongest);    // the whole picture
        const GeoBox bigger{40.0, -130.0, 55.0, -110.0};    // extends past it: still the strongest
        CHECK(frame.strongest_in(bigger) == strongest);
    }
    // a made-up picture: the strongest pixel wins, and a place outside is no rain
    {
        vtw::Image image;
        image.width = 4; image.height = 2;
        image.rgba.assign(4 * 2 * 4, 0);
        auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            uint8_t* p = &image.rgba[((size_t)y * 4 + x) * 4];
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        };
        put(1, 0, 14, 214, 20);                             // light green, 22
        put(2, 1, 255, 0, 0);                               // red, 50
        vtw::RadarFrame f;
        std::string error;
        CHECK(vtw::make_radar_frame(image, GeoBox{0, 0, 2, 4}, f, error));
        CHECK(f.at(1, 0) == 22 && f.at(2, 1) == 50 && f.at(0, 0) == 0);
        CHECK(f.value_at({1.5, 1.5}) == 22);                // row 0 is the north edge
        CHECK(f.value_at({0.5, 2.5}) == 50);
        CHECK(f.strongest_in(GeoBox{0, 0, 2, 4}) == 50);
        CHECK(f.strongest_in(GeoBox{1.0, 1.0, 2.0, 2.0}) == 22);
    }
    // refused input
    {
        vtw::RadarFrame f;
        std::string error;
        vtw::Image empty;
        CHECK(!vtw::make_radar_frame(empty, seattle, f, error) && f.width == 0);
        vtw::Image one;
        one.width = 1; one.height = 1; one.rgba.assign(4, 0);
        CHECK(!vtw::make_radar_frame(one, GeoBox{1, 1, 1, 2}, f, error));
        CHECK(vtw::make_radar_frame(one, GeoBox{1, 1, 2, 2}, f, error));
    }
}

}  // namespace

int main() {
    colour_tests();
    frame_tests();
    if (failures == 0) std::printf("all radar frame tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
