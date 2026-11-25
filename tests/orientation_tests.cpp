// Tests for which way is up: when the map turns (heading up, following, moving), how it eases round to a new
// direction the short way, and that the layers that depend on the view draw correctly with the map turned.
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "src/cell_grid.h"
#include "src/draw_grid.h"
#include "src/draw_radar.h"
#include "src/layers.h"
#include "src/orientation.h"

using namespace vtw;

namespace {

GpsFix moving_fix(double mph, double heading, bool valid = true) {
    GpsFix f;
    f.fix = true;
    f.lat = 31.8;
    f.lon = -92.0;
    f.speed_kmph = (mph + 1e-9) / KMPH_TO_MPH;
    f.heading_deg = heading;
    f.heading_valid = valid;
    return f;
}

}  // namespace

int main() {
    // ---- the shortest way round
    CHECK_NEAR(shortest_turn(0, 90), 90, 1e-9);
    CHECK_NEAR(shortest_turn(90, 0), -90, 1e-9);
    CHECK_NEAR(shortest_turn(350, 10), 20, 1e-9);            // through north, not the long way
    CHECK_NEAR(shortest_turn(10, 350), -20, 1e-9);
    CHECK_NEAR(shortest_turn(0, 180), 180, 1e-9);
    CHECK_NEAR(shortest_turn(0, 181), -179, 1e-9);
    CHECK_NEAR(shortest_turn(0, 179), 179, 1e-9);
    CHECK_NEAR(shortest_turn(720, 10), 10, 1e-9);
    CHECK_NEAR(shortest_turn(-90, 90), 180, 1e-9);
    CHECK_NEAR(shortest_turn(45, 45), 0, 1e-9);

    // ---- when the map turns: heading up, following, and a moving car with a direction; otherwise north up
    {
        Car moving;
        moving.update(GpsState::Fix, moving_fix(40, 123));
        CHECK(moving.arrow());
        CHECK_NEAR(heading_target(true, true, moving), 123, 1e-9);
        CHECK_NEAR(heading_target(false, true, moving), 0, 1e-9);             // not asked for
        CHECK_NEAR(heading_target(true, false, moving), 0, 1e-9);             // not following
        CHECK_NEAR(heading_target(false, false, moving), 0, 1e-9);
        Car slow;
        slow.update(GpsState::Fix, moving_fix(1, 123));
        CHECK_NEAR(heading_target(true, true, slow), 0, 1e-9);                // slow: north up
        Car unsure;
        unsure.update(GpsState::Fix, moving_fix(40, 123, false));
        CHECK_NEAR(heading_target(true, true, unsure), 0, 1e-9);              // no direction to go by
        Car none;
        CHECK_NEAR(heading_target(true, true, none), 0, 1e-9);                // no fix
        Car lost;
        lost.update(GpsState::Fix, moving_fix(40, 123));
        lost.update(GpsState::Stale, moving_fix(40, 123));
        CHECK_NEAR(heading_target(true, true, lost), 0, 1e-9);
        Car wrapped;
        wrapped.update(GpsState::Fix, moving_fix(40, 359.9));
        CHECK_NEAR(heading_target(true, true, wrapped), 359.9, 1e-9);
    }

    // ---- easing round: smooth, the short way, never past, and exactly there in the end
    {
        HeadingSmoother h;
        CHECK(h.heading() == 0);
        std::vector<double> seen;
        for (int i = 0; i < 150; i++) seen.push_back(h.update(90, 1.0 / 30));     // 5 seconds at 30 frames a second
        for (size_t i = 1; i < seen.size(); i++) {
            CHECK(seen[i] >= seen[i - 1]);                                         // only ever forward
            CHECK(seen[i] <= 90.0);                                                // never past it
            CHECK(seen[i] - seen[i - 1] <= TURN_RATE_DEG_PER_SECOND / 30 + 1e-9);  // never faster than the limit
        }
        CHECK(seen.back() == 90.0);                                                // exactly there
        CHECK(seen[10] > 20 && seen[10] < 90);                                     // it takes time: not a jump
        CHECK(seen[60] > 85);                                                      // but it is close within two seconds
        CHECK(seen[24] < 89.0);                                                    // and it eases in: still turning after 0.8 s (a steady turn would be done)
        CHECK(seen[5] - seen[4] > 3.5);                                            // quick at the start, at the limit
        CHECK(seen[30] - seen[29] < 1.5 && seen[30] - seen[29] > 0);               // and slow at the end
        CHECK(h.update(90, 1.0 / 30) == 90.0 && h.update(90, 1.0 / 30) == 90.0);   // and then it is the same every frame
    }
    {   // through north, forwards: 350 to 10
        HeadingSmoother h;
        for (int i = 0; i < 60; i++) h.update(350, 0.5);
        CHECK(h.heading() == 350.0);
        double previous = 350;
        double total = 0;
        for (int i = 0; i < 150; i++) {
            const double now = h.update(10, 1.0 / 30);
            const double step = shortest_turn(previous, now);
            CHECK(step >= 0 && step <= TURN_RATE_DEG_PER_SECOND / 30 + 1e-9);      // always clockwise, never round the long way
            total += step;
            previous = now;
        }
        CHECK(h.heading() == 10.0);
        CHECK_NEAR(total, 20.0, 1e-6);                                             // 20 degrees in all
    }
    {   // and backwards: 10 to 350
        HeadingSmoother h;
        for (int i = 0; i < 60; i++) h.update(10, 0.5);
        double previous = h.heading();
        for (int i = 0; i < 150; i++) {
            const double now = h.update(350, 1.0 / 30);
            CHECK(shortest_turn(previous, now) <= 0);
            previous = now;
        }
        CHECK(h.heading() == 350.0);
    }
    {   // exactly opposite: it still gets there
        HeadingSmoother h;
        for (int i = 0; i < 150; i++) h.update(180, 1.0 / 30);
        CHECK(h.heading() == 180.0);
    }
    {   // a stall: a long frame turns it no further than the half second allows; no time or backwards time, not at all
        HeadingSmoother h;
        const double a = h.update(170, 30.0);
        CHECK(a > 0 && a <= TURN_RATE_DEG_PER_SECOND * 0.5 + 1e-9);
        HeadingSmoother g;
        CHECK(g.update(170, 0.0) == 0.0);
        CHECK(g.update(170, -1.0) == 0.0);
    }
    {   // any start, any target, any frame time: closer every frame, within the limit, and it lands
        std::mt19937 rng(7);
        for (int trial = 0; trial < 300; trial++) {
            HeadingSmoother h;
            const double start = 5.0 + (double)(rng() % 3500) / 10.0, target = (double)(rng() % 3600) / 10.0;     // 5 to 355: not in the band round north
            for (int i = 0; i < 40; i++) h.update(start, 10.0);
            CHECK(std::fabs(shortest_turn(h.heading(), start)) < 1e-9);
            double left = std::fabs(shortest_turn(h.heading(), target));
            if (left < ENGAGE_DEGREES && std::fmod(target, 360.0) != 0.0) {        // inside the dead band: held, not moved
                for (int i = 0; i < 20; i++) CHECK(h.update(target, 0.05) == h.heading() && h.heading() == std::fmod(start, 360.0));
                continue;
            }
            bool landed = false;
            for (int i = 0; i < 400 && !landed; i++) {
                const double dt = 0.005 + (double)(rng() % 100) / 300.0;           // 5 ms to 0.34 s
                const double before = h.heading();
                const double now = h.update(target, dt);
                CHECK(std::fabs(shortest_turn(before, now)) <= TURN_RATE_DEG_PER_SECOND * dt + 1e-9);
                const double now_left = std::fabs(shortest_turn(now, target));
                CHECK(now_left <= left + 1e-9);
                left = now_left;
                landed = now == std::fmod(target + 360.0, 360.0) || left == 0.0;
            }
            CHECK(landed);
            CHECK(h.heading() >= 0 && h.heading() < 360);
        }
    }
    {   // a corner as the GPS tells it: a right turn of 90 degrees over 5 seconds, a heading once a second
        HeadingSmoother h;
        for (int i = 0; i < 90; i++) h.update(90, 1.0 / 30);                       // going east, settled
        double target = 90, worst_lag = 0;
        for (int second = 0; second < 8; second++) {
            target = second < 5 ? 90 + 18.0 * (second + 1) : 180;
            for (int frame = 0; frame < 30; frame++) {
                h.update(target, 1.0 / 30);
                worst_lag = std::max(worst_lag, std::fabs(shortest_turn(h.heading(), target)));
            }
        }
        CHECK(worst_lag < 25.0);                                                   // it keeps up with the car
        CHECK(h.heading() == 180.0);
        for (int i = 0; i < 120; i++) h.update(0, 1.0 / 30);                       // stopped: back to north up in a few seconds
        CHECK(h.heading() == 0.0);
    }
    {   // the dead band: a settled map ignores a direction less than 2 degrees off, and goes all the way once it starts
        HeadingSmoother h;
        for (int i = 0; i < 200; i++) h.update(90, 1.0 / 30);
        CHECK(h.heading() == 90.0);
        for (int i = 0; i < 30; i++) CHECK(h.update(91.9, 1.0 / 30) == 90.0);        // a wobble: not moved
        for (int i = 0; i < 30; i++) CHECK(h.update(88.2, 1.0 / 30) == 90.0);
        const double started = h.update(92.5, 1.0 / 30);                             // far enough: it starts...
        CHECK(started > 90.0 && started < 92.5);
        for (int i = 0; i < 200; i++) h.update(92.5, 1.0 / 30);
        CHECK(h.heading() == 92.5);                                                  // ...and lands exactly on it
        // once it has started it keeps going even as the target comes back within the band
        HeadingSmoother g;
        for (int i = 0; i < 200; i++) g.update(90, 1.0 / 30);
        g.update(95, 1.0 / 30);
        for (int i = 0; i < 200; i++) g.update(91, 1.0 / 30);
        CHECK(g.heading() == 91.0);
        // north up is always exact: from a bit off north, a target of 0 is reached
        HeadingSmoother n;
        for (int i = 0; i < 200; i++) n.update(1.5, 1.0 / 30);
        CHECK(n.heading() == 0.0);                                                   // 1.5 degrees from north: held (in the band)
        for (int i = 0; i < 200; i++) n.update(40, 1.0 / 30);
        CHECK(n.heading() == 40.0);
        for (int i = 0; i < 200; i++) n.update(41.5, 1.0 / 30);
        CHECK(n.heading() == 40.0);
        for (int i = 0; i < 200; i++) n.update(0, 1.0 / 30);
        CHECK(n.heading() == 0.0);
        HeadingSmoother m;
        for (int i = 0; i < 200; i++) m.update(1.0, 1.0 / 30);                       // a wobble that has pulled it 1 degree off...
        CHECK(m.heading() == 0.0);
        for (int i = 0; i < 200; i++) m.update(30, 1.0 / 30);
        for (int i = 0; i < 200; i++) m.update(1.5, 1.0 / 30);                       // and then settles, from far away, onto 1.5
        CHECK(m.heading() == 1.5);
        for (int i = 0; i < 3; i++) m.update(0, 1.0 / 30);                           // north up asked for: it moves at once, however near
        CHECK(m.heading() < 1.5);
        for (int i = 0; i < 100; i++) m.update(0, 1.0 / 30);
        CHECK(m.heading() == 0.0);
    }
    {   // a target given as a negative angle, or more than a turn
        HeadingSmoother h;
        for (int i = 0; i < 200; i++) h.update(-90, 1.0 / 30);
        CHECK(h.heading() == 270.0);
        HeadingSmoother g;
        for (int i = 0; i < 200; i++) g.update(450, 1.0 / 30);
        CHECK(g.heading() == 90.0);
    }
    {   // north up when it is already north up: nothing moves, a heading that is 360 is 0
        HeadingSmoother h;
        CHECK(h.update(0, 1.0 / 30) == 0.0);
        CHECK(h.update(360, 1.0 / 30) == 0.0);
        CHECK(h.update(720.0, 1.0 / 30) == 0.0);
    }

    // ---- the layers with the map turned
    for (double heading : {0.0, 33.0, 90.0, 180.0, 270.0, 359.0}) {
        View view;
        view.set_cell_aspect(2.0);
        view.set_centre({31.8, -92.0});
        view.set_zoom_km(80);
        view.set_size(100, 30);
        view.set_heading(heading);
        const CellGrid grid(view);

        // rain: one small patch of heavy rain at a place a little east and north of the centre (3 km by 3 km)
        const GeoPoint spot = view.at(70.5, 8.5);
        RadarFrame frame;
        frame.box = view.box(0.2);
        frame.width = 400; frame.height = 400;
        frame.dbz.assign((size_t)frame.width * frame.height, 0);
        const double dlat = frame.box.north - frame.box.south, dlon = frame.box.east - frame.box.west;
        for (int y = 0; y < frame.height; y++)
            for (int x = 0; x < frame.width; x++) {
                const double lat = frame.box.north - (y + 0.5) / frame.height * dlat;
                const double lon = frame.box.west + (x + 0.5) / frame.width * dlon;
                if (distance_km({lat, lon}, spot) < 3.0) frame.dbz[(size_t)y * frame.width + x] = 50;
            }
        vtscreen::Picture rain;
        rain.init(30, 100);
        LayerBlend blend(rain);
        draw_radar(grid, frame, blend);
        double col, row;
        view.to_cell(spot, col, row);
        CHECK(rain.at((int)row, (int)col).lum > 0);                          // the rain is where the place is on the turned screen
        int lit = 0;
        for (const vtscreen::PicCell& c : rain.cell) if (c.lum > 0) lit++;
        CHECK(lit >= 1 && lit < 60);                                         // a patch, not smeared over the screen

        // the grid of lines: the spacing comes from the ground distance, so a turned map has the same few lines
        vtscreen::Picture lines;
        lines.init(30, 100);
        LayerBlend line_blend(lines);
        draw_graticule(view, grid, lines, line_blend);
        int grid_cells = 0;
        for (int r = 0; r < 30; r++) for (int c = 0; c < 100; c++) if (lines.at(r, c).lum > 0) grid_cells++;
        CHECK(grid_cells > 20);                                              // there are lines
        CHECK(grid_cells < 1500);                                            // and not a flood: under half the screen
        // the labels have the same number of decimals however the map is turned
        std::string text;
        for (const vtscreen::PicCell& c : lines.cell) if (c.text) text += (char)(c.ch < 128 ? c.ch : '?');
        // the labels have one decimal at this zoom (lines every 0.2 degrees), never more
        for (size_t i = 0; i + 2 < text.size(); i++)
            CHECK(!(text[i] == '.' && text[i + 1] >= '0' && text[i + 1] <= '9' && text[i + 2] >= '0' && text[i + 2] <= '9'));
    }

    if (failures == 0) std::printf("all orientation tests passed\n");
    return failures == 0 ? 0 : 1;
}
