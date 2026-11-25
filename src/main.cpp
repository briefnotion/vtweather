// vtweather - an overhead map of where you are, with the real-time rain on it, drawn as
// letters in a terminal by vtscreen.
//
// The ground (land, water and coast, from data/basemap.bin) is drawn first, dimly; the grid of
// latitude and longitude (g) and a marker for home sit on it. On top of it goes the rain, from
// NOAA's radar. The last radar pictures and where you were looking are kept in a cache folder
// between runs (--no-cache turns that off). The loop is the one from the design:
// setup, then gather, output, show and wait, again and again. The radar picture is fetched on
// a thread of its own (RadarFeed), so nothing here ever waits for the network.
//
//   ./build/vtweather [--settings FILE] [--basemap FILE] [--offline] [--no-cache] [--no-color]
//                     [--fps N] [--cols N --rows N]
//
// Keys: + - zoom, w a s d move, r back to the settings' place, g grid, n city names, / go to a city, q quit. --offline asks the
// radar service for nothing (for trying the keys, or with no network).

#include <chrono>
#include <cstdio>
#include <string>

#include "basemap.h"
#include "cards.h"
#include "draw_grid.h"
#include "draw_ground.h"
#include "draw_places.h"
#include "draw_radar.h"
#include "gps_feed.h"
#include "ground_map.h"
#include "input.h"
#include "layers.h"
#include "keys.h"
#include "prompt.h"
#include "place_labels.h"
#include "radar_feed.h"
#include "store.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

int main(int argc, char** argv) {
    // setup
    const std::string settings_path = vtw::take_settings_option(argc, argv);
    const std::string basemap_path = vtw::take_settings_option(argc, argv, "--basemap");
    const bool offline = vtw::take_flag(argc, argv, "--offline");
    const bool no_cache = vtw::take_flag(argc, argv, "--no-cache");
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

    vtw::BaseMap basemap;                    // the land and water; without it the screen has no ground
    {
        const std::string file = vtw::find_basemap_file(basemap_path);
        std::string problem;
        if (file.empty()) notes += "no base map file (data/basemap.bin): there will be no ground\n";
        else if (!vtw::load_basemap(file, basemap, problem)) notes += "base map: " + problem + "\n";
    }
    vtw::GroundMap ground(basemap);
    vtw::PlaceLabels places(basemap);
    vtw::UiState ui;
    vtw::KeyReader reader;
    vtw::CardsState cards;                   // when the keys card and the notes card are shown

    // the folder for what is kept between runs (none with --no-cache or no home folder)
    const std::string cache_dir = no_cache ? "" : (settings.cache_dir.empty() ? vtw::default_cache_dir() : settings.cache_dir);

    vtw::View view;
    view.set_cell_aspect(settings.cell_aspect);
    view.set_centre(settings.home);
    view.set_zoom_km(settings.zoom_km);
    view.set_size(cols, rows - 1);                 // the last row is the status bar

    // start where you were looking last time, if you want that and there is something remembered
    vtw::ViewState saved_view;
    if (settings.remember_view && !cache_dir.empty() && vtw::load_view_state(cache_dir, saved_view)) {
        view.set_centre(saved_view.centre);
        view.set_zoom_km(saved_view.zoom_km);
        ui.graticule = saved_view.graticule;
        ui.names = saved_view.names;
        ui.legend = saved_view.legend;
    }
    auto current_view_state = [&]() {
        vtw::ViewState state;
        state.centre = view.centre();
        state.zoom_km = view.zoom_km();
        state.graticule = ui.graticule;
        state.names = ui.names;
        state.legend = ui.legend;
        return state;
    };
    vtw::ViewState last_saved = current_view_state();
    double last_save_time = 0;

    // asks for the first picture on the first frame (not at all with --offline)
    vtw::RadarStore store(cache_dir);
    const double unix_at_start = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    vtw::RadarFeed radar(settings, cache_dir.empty() ? nullptr : &store, unix_at_start);
    radar.set_offline(offline);

    vtw::GpsFeed gps(settings.gps_file);       // the car's position, from the file rasfled2 writes

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

        // gather: keys, which become commands on the view (or typing, while the go-to prompt is open)
        for (char c; (c = display.next_key()) != 0;) reader.feed(c);
        reader.flush();
        for (vtw::Key key; reader.next(key);) {
            cards.key_pressed(t);
            if (ui.prompt.active) vtw::prompt_key(ui.prompt, key, basemap, view);
            else vtw::apply_command(vtw::command_for_event(key), view, settings, ui);
        }
        display.quit_on_q = !ui.prompt.active;        // while typing, a q is a letter

        // gather: a new radar picture, if one has arrived, and a request if it is time
        radar.update(view, t);
        const double unix_now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        gps.update(unix_now);

        // output
        picture.init(rows - 1, cols);
        const vtw::CellGrid grid(view);
        vtw::LayerBlend blend(picture);               // where layers overlap, the cell shows it
        ground.update(view);
        vtw::draw_ground(ground, picture, blend);
        if (ui.graticule) vtw::draw_graticule(view, grid, picture, blend);
        vtw::draw_radar(grid, radar.frame(), blend);
        if (ui.names) {
            places.update(view);
            vtw::draw_places(places, picture);
        }
        vtw::draw_marker(view, settings.home, picture);
        vtw::draw_cards(picture, view, settings, notes, radar.status(t), cards.help_visible(t, ui.prompt.active),
                        cards.notes_visible(notes), ui.legend && radar.has_frame() && !ui.prompt.active,
                        vtw::gps_status_text(gps, unix_now));
        vtw::draw_prompt(ui.prompt, basemap, picture);
        display.bar(picture, info, t, true);

        // show, then wait for the next frame
        display.show(picture, t);
        display.end_frame();

        // remember where you are looking, now and then (and when you leave)
        if (!cache_dir.empty() && settings.remember_view && t - last_save_time >= 30) {
            last_save_time = t;
            const vtw::ViewState now_state = current_view_state();
            std::string ignored;
            if (now_state.centre.lat != last_saved.centre.lat || now_state.centre.lon != last_saved.centre.lon ||
                now_state.zoom_km != last_saved.zoom_km || now_state.graticule != last_saved.graticule ||
                now_state.names != last_saved.names || now_state.legend != last_saved.legend) {
                vtw::save_view_state(cache_dir, now_state, ignored);
                last_saved = now_state;
            }
        }
    }
    if (!cache_dir.empty() && settings.remember_view) {
        std::string ignored;
        vtw::save_view_state(cache_dir, current_view_state(), ignored);
    }
    return 0;
}
