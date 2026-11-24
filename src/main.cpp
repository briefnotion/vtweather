// vtweather - an overhead map of where you are, with the real-time rain on it, drawn as
// letters in a terminal by vtscreen.
//
// There is no base map yet: a graticule (lines of latitude and longitude) and a marker stand
// in for it. On top of it goes the rain, from NOAA's radar. The loop is the one from the design:
// setup, then gather, output, show and wait, again and again. The radar picture is fetched on
// a thread of its own (RadarFeed), so nothing here ever waits for the network.
//
//   ./build/vtweather [--settings FILE] [--offline] [--no-color] [--fps N] [--cols N --rows N]
//
// Keys: + - zoom, w a s d move, r back to the settings' place, q quit. --offline asks the
// radar service for nothing (for trying the keys, or with no network).

#include <cstdio>
#include <memory>
#include <string>

#include "draw_grid.h"
#include "draw_radar.h"
#include "input.h"
#include "radar_feed.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

int main(int argc, char** argv) {
    // setup
    const std::string settings_path = vtw::take_settings_option(argc, argv);
    const bool offline = vtw::take_flag(argc, argv, "--offline");
    vtscreen::Options options;
    int rc = vtscreen::parse_options(argc, argv, options);
    if (rc >= 0) return rc;
    if (options.still) {
        std::fprintf(stderr, "vtweather: --still is not supported\n");
        return 2;
    }
    int cols, rows;
    if (!vtscreen::resolve_size(options, cols, rows)) return 1;

    std::string notes;
    const vtw::Settings settings = vtw::load_settings(settings_path, notes);

    vtw::View view;
    view.set_cell_aspect(settings.cell_aspect);
    view.set_centre(settings.home);
    view.set_zoom_km(settings.zoom_km);
    view.set_size(cols, rows - 1);                 // the last row is the status bar

    // asks for the first picture on the first frame (not at all with --offline)
    std::unique_ptr<vtw::RadarFeed> radar;
    if (!offline) radar.reset(new vtw::RadarFeed(settings));
    const vtw::RadarFrame no_rain;

    vtscreen::Display display;
    display.open(options, cols, rows);
    display.start();
    const vtscreen::Info info{"vtweather", 0.0, {90, 160, 220}};
    vtscreen::Picture picture;

    while (!display.stopping()) {
        const double t = display.time();

        // gather: the window may have been resized
        if (vtscreen::follow_resize(display, options, cols, rows)) {
            display.resize(cols, rows);
            view.set_size(cols, rows - 1);
        }

        // gather: keys, which become commands on the view
        for (char key; (key = display.next_key()) != 0;)
            vtw::apply_command(vtw::command_for_key(key), view, settings);

        // gather: a new radar picture, if one has arrived, and a request if it is time
        if (radar) radar->update(view, t);

        // output
        picture.init(rows - 1, cols);
        const vtw::CellGrid grid(view);
        vtw::draw_graticule(view, grid, picture);
        vtw::draw_radar(grid, radar ? radar->frame() : no_rain, picture);
        vtw::draw_marker(view, settings.home, picture);
        vtw::draw_readout(view, settings, notes, radar ? radar->status(t) : "radar: off (--offline)", picture);
        display.bar(picture, info, t, true);

        // show, then wait for the next frame
        display.show(picture, t);
        display.end_frame();
    }
    return 0;
}
