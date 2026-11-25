// The cards on screen and where they go. A card is the little framed window from card.h; this is
// what each one says and how they are stacked.
//
//   top right      the view card (the place, where the map is centred, the scale, the radar's
//                  scan time), always shown; under it the notes card, the warnings from starting
//                  up (a settings file that was not found, say), which stays until the first key
//                  press; under that the keys card, which is shown at the start and again for
//                  HELP_SECONDS after any key press, so it is there while you are moving about and
//                  goes away when you are not
//   bottom right  the rain card, the key to the rain (k shows and hides it): a line of words and a
//                  strip from light to heavy rain in the cells the rain is drawn with
//   bottom left    the driving card (the GPS position, speed and heading), when there is a GPS file
//
// A card that does not fit under the ones above it is left out; the top right stack never reaches the
// rows at the bottom that the rain card uses.
#pragma once

#include <string>

#include "car.h"
#include "card.h"
#include "draw_radar.h"
#include "gps_feed.h"
#include "settings.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

constexpr double HELP_SECONDS = 30;        // how long the keys card stays after the last key press
constexpr int CARDS_FREE_ROWS_AT_BOTTOM = 4;     // the rain card takes the last four rows above the status bar

// When the cards that come and go are shown.
struct CardsState {
    double last_key_time = 0;              // the keys card shows at the start, as if a key had just been pressed
    bool notes_dismissed = false;

    void key_pressed(double now) { last_key_time = now; notes_dismissed = true; }
    // not while the go-to prompt is open: it has its own box
    bool help_visible(double now, bool prompt_open) const { return !prompt_open && now - last_key_time < HELP_SECONDS; }
    bool notes_visible(const std::string& notes) const { return !notes_dismissed && !notes.empty(); }
};

// The last line says which way is up: "north up", or with the map turned where north has gone ("heading up, north ↗"),
// or "heading up (north up while slow)" when heading up is on but the map is not turned just now.
Card view_card(const View& view, const Settings& settings, const std::string& radar_status, bool heading_up = false);
Card help_card();
Card notes_card(const std::string& notes);       // one line per note (the notes are separated by newlines)
Card legend_card();                              // the key to the rain

// The driving card: what the GPS says, in miles per hour and feet. With a fix it has the position, the speed (and
// the heading while the car counts as moving) and the height and satellites; with no fix, or when the data is lost
// or unreadable, it says so in amber. It has no lines (so nothing is drawn) when there is no GPS to talk about: no
// file named, or no file at all. age_seconds is how long ago the file was written. While the map follows the car
// the title says so.
Card driving_card(GpsState state, const GpsFix& fix, const std::string& problem, double age_seconds, const Car& car,
                  bool following = false);

// Draw the cards that are due: the stack in the top right corner, and the rain card in the bottom
// right if show_legend is true and the screen is big enough for it, and the driving card (if it has lines) in the
// bottom left.
void draw_cards(vtscreen::Picture& picture, const View& view, const Settings& settings, const std::string& notes,
                const std::string& radar_status, bool show_help, bool show_notes, bool show_legend,
                const Card* driving = nullptr, bool heading_up = false);

}  // namespace vtw
