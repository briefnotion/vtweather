// vtweather - an overhead map of where you are, with the real-time rain on it,
// drawn as letters in a terminal by vtscreen. For now: the skeleton. It opens the
// display, follows the window being resized, shows a blank picture and the status
// bar, and quits on q.
//
// The loop will grow into the stages from the design: gather (web data, keys, resize),
// process (decode, convert), output (draw the picture), show, wait.

#include <cstdio>

#include "vtscreen/vtscreen.h"

int main(int argc, char** argv) {
    // setup
    vtscreen::Options options;
    int rc = vtscreen::parse_options(argc, argv, options);
    if (rc >= 0) return rc;
    if (options.still) {
        std::fprintf(stderr, "vtweather: --still is not supported\n");
        return 2;
    }
    int cols, rows;
    if (!vtscreen::resolve_size(options, cols, rows)) return 1;

    vtscreen::Display display;
    display.open(options, cols, rows);
    display.start();
    const vtscreen::Info info{"vtweather", 0.0, {90, 160, 220}};
    vtscreen::Picture picture;

    while (!display.stopping()) {
        const double t = display.time();

        // gather: the window may have been resized
        if (vtscreen::follow_resize(display, options, cols, rows)) display.resize(cols, rows);

        // output: a blank picture with a line of text and the status bar
        picture.init(rows - 1, cols);
        picture.text(1, 2, "vtweather: nothing to show yet (q quits)");
        display.bar(picture, info, t, true);

        // show, then wait for the next frame
        display.show(picture, t);
        display.end_frame();
    }
    return 0;
}
