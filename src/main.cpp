// vtweather - an overhead map of where you are, with the real-time rain on it, drawn as
// letters in a terminal by vtscreen.
//
// Right now there is no map yet: a graticule (lines of latitude and longitude) and a
// marker stand in for it, to show that the View places things correctly. The loop is
// the one from the design: setup, then gather, output, show and wait, again and again.
//
//   ./build/vtweather [--settings FILE] [--no-color] [--fps N] [--cols N --rows N]

#include <cstdio>
#include <string>

#include "draw_grid.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

int main(int argc, char** argv) {
    // setup
    const std::string settings_path = vtw::take_settings_option(argc, argv);
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

        // output
        picture.init(rows - 1, cols);
        vtw::draw_graticule(view, picture);
        vtw::draw_marker(view, settings.home, picture);
        vtw::draw_readout(view, settings, notes, picture);
        display.bar(picture, info, t, true);

        // show, then wait for the next frame
        display.show(picture, t);
        display.end_frame();
    }
    return 0;
}
