// Tests for the car: when it counts as moving (with a gap so it does not flicker), holding the position
// while slow, starting from a fresh fix, the arrow and its eight directions, and drawing it on the map.
#include <string>

#include "check.h"
#include "src/car.h"

using namespace vtw;

namespace {

GpsFix reading(double lat, double lon, double mph, double heading = 90, bool heading_valid = true) {
    GpsFix f;
    f.fix = true;
    f.lat = lat;
    f.lon = lon;
    f.speed_kmph = (mph + 1e-9) / KMPH_TO_MPH;               // the 1e-9 so that a round trip through km/h cannot land a hair below the speed
    f.heading_deg = heading;
    f.heading_valid = heading_valid;
    return f;
}

char32_t at(const vtscreen::Picture& p, int row, int col) {
    const vtscreen::PicCell& c = p.at(row, col);
    return c.text ? c.ch : U' ';
}

}  // namespace

int main() {
    // ---- the units
    CHECK_NEAR(KMPH_TO_MPH * 100.0, 62.1371, 1e-9);
    CHECK_NEAR(METERS_TO_FEET * 1000.0, 3280.84, 1e-9);

    // ---- nothing until there is a fix
    {
        Car car;
        CHECK(!car.visible() && !car.arrow() && !car.moving());
        car.update(GpsState::NoFile, reading(1, 2, 60));
        CHECK(!car.visible());
        car.update(GpsState::NoFix, reading(1, 2, 60));
        CHECK(!car.visible());
        car.update(GpsState::Stale, reading(1, 2, 60));
        CHECK(!car.visible());
        car.update(GpsState::Bad, reading(1, 2, 60));
        CHECK(!car.visible());
        car.update(GpsState::Off, reading(1, 2, 60));
        CHECK(!car.visible());
    }

    // ---- moving: the live position and an arrow
    {
        Car car;
        car.update(GpsState::Fix, reading(37.0, -97.0, 60, 123));
        CHECK(car.visible() && car.moving() && car.arrow());
        CHECK_NEAR(car.position().lat, 37.0, 1e-12);
        CHECK_NEAR(car.position().lon, -97.0, 1e-12);
        CHECK_NEAR(car.heading(), 123, 1e-9);
        CHECK_NEAR(car.speed_mph(), 60, 1e-6);
        car.update(GpsState::Fix, reading(37.1, -97.1, 61, 124));
        CHECK_NEAR(car.position().lat, 37.1, 1e-12);                 // follows the live position while moving
        CHECK_NEAR(car.position().lon, -97.1, 1e-12);
        // moving, but the GPS does not trust the heading: no arrow
        car.update(GpsState::Fix, reading(37.2, -97.2, 61, 124, false));
        CHECK(car.moving() && !car.arrow());
    }

    // ---- the gap: on at 5 mph, off only below 3
    {
        Car car;                                                      // moving at 5 mph by default
        car.update(GpsState::Fix, reading(1, 1, 4.9));
        CHECK(!car.moving());
        car.update(GpsState::Fix, reading(1, 1, 5.0));
        CHECK(car.moving());
        car.update(GpsState::Fix, reading(1, 1, 4.0));
        CHECK(car.moving());                                          // still moving: in the gap
        car.update(GpsState::Fix, reading(1, 1, 3.01));
        CHECK(car.moving());
        car.update(GpsState::Fix, reading(1, 1, 2.9));
        CHECK(!car.moving());
        car.update(GpsState::Fix, reading(1, 1, 4.0));
        CHECK(!car.moving());                                         // and in the gap it stays off until 5
        car.update(GpsState::Fix, reading(1, 1, 5.01));
        CHECK(car.moving());
        // another limit
        Car slow(10.0);
        slow.update(GpsState::Fix, reading(1, 1, 9.9));
        CHECK(!slow.moving());
        slow.update(GpsState::Fix, reading(1, 1, 10.0));
        CHECK(slow.moving());
        slow.update(GpsState::Fix, reading(1, 1, 6.1));
        CHECK(slow.moving());
        slow.update(GpsState::Fix, reading(1, 1, 5.9));
        CHECK(!slow.moving());
    }

    // ---- slow: the position is held where it was, however the fix wanders
    {
        Car car;
        car.update(GpsState::Fix, reading(37.0, -97.0, 40));          // driving
        car.update(GpsState::Fix, reading(37.001, -97.001, 20));      // slowing, still moving: follows
        car.update(GpsState::Fix, reading(37.002, -97.002, 2));       // stopped: this one is the last that counts... and then it is held
        const GeoPoint held = car.position();
        CHECK(!car.moving());
        CHECK(!car.arrow());
        CHECK_NEAR(held.lat, 37.001, 1e-12);                          // held at the last position while it was moving
        CHECK_NEAR(held.lon, -97.001, 1e-12);
        for (int i = 0; i < 10; i++)                                  // the drift at a stop
            car.update(GpsState::Fix, reading(37.002 + 0.0001 * i, -97.002 - 0.0001 * i, 0.3 + 0.1 * i));
        CHECK_NEAR(car.position().lat, held.lat, 1e-12);
        CHECK_NEAR(car.position().lon, held.lon, 1e-12);
        CHECK_NEAR(car.speed_mph(), 1.2, 1e-6);                       // the live speed is still known
        car.update(GpsState::Fix, reading(37.01, -97.01, 12));        // moving off: back to the live position
        CHECK(car.moving());
        CHECK_NEAR(car.position().lat, 37.01, 1e-12);
    }

    // ---- the first fix and a fix that comes back start from the live position, whatever the speed
    {
        Car car;
        car.update(GpsState::Fix, reading(10.0, 20.0, 0));            // stopped from the first fix
        CHECK(car.visible() && !car.moving());
        CHECK_NEAR(car.position().lat, 10.0, 1e-12);
        car.update(GpsState::Fix, reading(10.5, 20.5, 0));
        CHECK_NEAR(car.position().lat, 10.0, 1e-12);                  // held
        car.update(GpsState::Stale, reading(10.5, 20.5, 0));          // the GPS is lost
        CHECK(!car.visible());
        car.update(GpsState::Fix, reading(50.0, 60.0, 1));            // it is back, a long way off, and slow
        CHECK(car.visible());
        CHECK_NEAR(car.position().lat, 50.0, 1e-12);                  // not the old held position
        CHECK_NEAR(car.position().lon, 60.0, 1e-12);
        car.update(GpsState::NoFix, reading(50.0, 60.0, 1));          // no fix for a moment, also loses the hold
        car.update(GpsState::Fix, reading(51.0, 61.0, 1));
        CHECK_NEAR(car.position().lat, 51.0, 1e-12);
        // moving state does not carry over a lost fix either
        Car moving;
        moving.update(GpsState::Fix, reading(1, 1, 30));
        CHECK(moving.moving());
        moving.update(GpsState::Stale, reading(1, 1, 30));
        CHECK(!moving.moving());
        moving.update(GpsState::Fix, reading(2, 2, 4));
        CHECK(!moving.moving());                                      // 4 mph is under the limit to turn on
    }

    // ---- the arrows and names, in steps of 45 degrees
    {
        struct { double h; const char* arrow; const char* name; } cases[] = {
            {0, "↑", "N"}, {22.4, "↑", "N"}, {22.6, "↗", "NE"}, {45, "↗", "NE"}, {67.4, "↗", "NE"}, {67.6, "→", "E"}, {90, "→", "E"},
            {135, "↘", "SE"}, {180, "↓", "S"}, {225, "↙", "SW"}, {270, "←", "W"}, {315, "↖", "NW"}, {337.4, "↖", "NW"},
            {337.6, "↑", "N"}, {359.9, "↑", "N"}, {360, "↑", "N"}, {405, "↗", "NE"}, {-45, "↖", "NW"}, {-90, "←", "W"}};
        for (const auto& c : cases) {
            CHECK(std::string(arrow_glyph(c.h, 0)) == c.arrow);
            CHECK(std::string(compass_name(c.h)) == c.name);
        }
        // with the map turned, up is a different direction: heading east on a map whose up is east points up
        CHECK(std::string(arrow_glyph(90, 90)) == "↑");
        CHECK(std::string(arrow_glyph(90, 0)) == "→");
        CHECK(std::string(arrow_glyph(0, 90)) == "←");
        CHECK(std::string(arrow_glyph(180, 90)) == "→");
    }

    // ---- drawing the car on the map
    {
        View view;
        view.set_cell_aspect(2.0);
        view.set_centre({37.0, -97.0});
        view.set_zoom_km(100);
        view.set_size(100, 30);
        vtscreen::Picture picture;
        picture.init(30, 100);

        // not visible: nothing
        Car car;
        draw_car(view, car, picture);
        for (const vtscreen::PicCell& c : picture.cell) CHECK(!c.text);

        // moving east: an arrow in the middle of the screen (the car is at the centre)
        car.update(GpsState::Fix, reading(37.0, -97.0, 50, 90));
        draw_car(view, car, picture);
        double col, row;
        view.to_cell({37.0, -97.0}, col, row);
        CHECK(at(picture, (int)row, (int)col) == U'→');
        const vtscreen::PicCell& cell = picture.at((int)row, (int)col);
        CHECK(cell.r == 255 && cell.g == 255 && cell.b == 255);        // white, not the home marker's yellow
        CHECK(cell.alt_lum > 0);                                       // on a dark background
        int drawn = 0;
        for (const vtscreen::PicCell& c : picture.cell) if (c.text) drawn++;
        CHECK(drawn == 1);

        // slow: a +
        vtscreen::Picture slow;
        slow.init(30, 100);
        Car stopped;
        stopped.update(GpsState::Fix, reading(37.0, -97.0, 0.5, 90));
        draw_car(view, stopped, slow);
        CHECK(at(slow, (int)row, (int)col) == U'+');

        // north up and heading up: the arrow follows the turn of the view
        view.set_heading(90);
        vtscreen::Picture turned;
        turned.init(30, 100);
        draw_car(view, car, turned);
        view.to_cell({37.0, -97.0}, col, row);
        CHECK(at(turned, (int)row, (int)col) == U'↑');
        view.set_heading(0);

        // off the screen: nothing
        vtscreen::Picture away;
        away.init(30, 100);
        Car far;
        far.update(GpsState::Fix, reading(45.0, -97.0, 50, 0));
        draw_car(view, far, away);
        for (const vtscreen::PicCell& c : away.cell) CHECK(!c.text);

        // at the edges of the screen: drawn at the first and last cells, never outside
        for (const GeoPoint& p : {view.at(0.2, 0.2), view.at(99.8, 0.2), view.at(0.2, 29.8), view.at(99.8, 29.8)}) {
            vtscreen::Picture edge;
            edge.init(30, 100);
            Car at_edge;
            at_edge.update(GpsState::Fix, reading(p.lat, p.lon, 50, 0));
            draw_car(view, at_edge, edge);
            int count = 0;
            for (const vtscreen::PicCell& c : edge.cell) if (c.text) count++;
            CHECK(count == 1);
        }
        // just off the edge: nothing
        for (const GeoPoint& p : {view.at(-0.5, 10), view.at(100.5, 10), view.at(50, -0.5), view.at(50, 30.5)}) {
            vtscreen::Picture off;
            off.init(30, 100);
            Car outside;
            outside.update(GpsState::Fix, reading(p.lat, p.lon, 50, 0));
            draw_car(view, outside, off);
            for (const vtscreen::PicCell& c : off.cell) CHECK(!c.text);
        }
    }

    // ---- following: the map stays on the car, when it is meant to and there is a fix
    {
        View view;
        view.set_cell_aspect(2.0);
        view.set_centre({10.0, 20.0});
        view.set_zoom_km(75);
        view.set_size(100, 30);

        Car car;
        CHECK(!follow_car(true, car, view));                   // no fix: the map stays where it is
        CHECK_NEAR(view.centre().lat, 10.0, 1e-12);
        car.update(GpsState::Fix, reading(37.5, -97.5, 60));
        CHECK(!follow_car(false, car, view));                  // not following: the map stays where it is
        CHECK_NEAR(view.centre().lat, 10.0, 1e-12);
        CHECK_NEAR(view.centre().lon, 20.0, 1e-12);
        CHECK(follow_car(true, car, view));                    // following: centred on the car, the zoom untouched
        CHECK_NEAR(view.centre().lat, 37.5, 1e-12);
        CHECK_NEAR(view.centre().lon, -97.5, 1e-12);
        CHECK_NEAR(view.zoom_km(), 75, 1e-12);
        car.update(GpsState::Fix, reading(37.6, -97.6, 60));
        follow_car(true, car, view);
        CHECK_NEAR(view.centre().lat, 37.6, 1e-12);            // and it keeps up with the car

        // slow: the map follows the HELD position, so it does not crawl about with the drift
        car.update(GpsState::Fix, reading(37.601, -97.601, 1));
        follow_car(true, car, view);
        const GeoPoint held = view.centre();
        for (int i = 0; i < 5; i++) {
            car.update(GpsState::Fix, reading(37.601 + 0.0003 * i, -97.601 - 0.0003 * i, 0.5));
            follow_car(true, car, view);
            CHECK_NEAR(view.centre().lat, held.lat, 1e-12);
            CHECK_NEAR(view.centre().lon, held.lon, 1e-12);
        }

        // the GPS is lost: it stops following (the map stays), and picks up again when the fix is back
        car.update(GpsState::Stale, reading(1, 1, 60));
        CHECK(!follow_car(true, car, view));
        CHECK_NEAR(view.centre().lat, held.lat, 1e-12);
        car.update(GpsState::Fix, reading(40.0, -100.0, 60));
        CHECK(follow_car(true, car, view));
        CHECK_NEAR(view.centre().lat, 40.0, 1e-12);

        // the centre is kept inside what the view allows
        car.update(GpsState::Fix, reading(84.0, 10.0, 60));
        follow_car(true, car, view);
        CHECK(view.centre().lat <= MAX_CENTRE_LATITUDE + 1e-9);
    }

    if (failures == 0) std::printf("all car tests passed\n");
    return failures == 0 ? 0 : 1;
}
