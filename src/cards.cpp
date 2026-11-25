#include "cards.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vtw {

namespace {

std::string degrees_text(double value, char positive, char negative) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f%c", std::fabs(value), value >= 0 ? positive : negative);
    return text;
}

const Rgb KEY_COLOUR{235, 235, 235};

CardLine plain(const std::string& text, Rgb colour) { return {"", text, KEY_COLOUR, colour}; }
CardLine keyed(const std::string& key, const std::string& text, Rgb colour) { return {key, text, KEY_COLOUR, colour}; }

}  // namespace

Card view_card(const View& view, const Settings& settings, const std::string& radar_status) {
    Card card;
    card.title = settings.place_name;
    char text[160];
    const GeoPoint centre = view.centre();
    std::snprintf(text, sizeof text, "centre %s %s", degrees_text(centre.lat, 'N', 'S').c_str(),
                  degrees_text(centre.lon, 'E', 'W').c_str());
    card.lines.push_back(plain(text, {170, 170, 170}));
    std::snprintf(text, sizeof text, "%.0f km across, %.2f km per column", view.zoom_km(), view.km_per_col());
    card.lines.push_back(plain(text, {170, 170, 170}));
    card.lines.push_back(plain(radar_status, {150, 200, 150}));
    return card;
}

Card help_card() {
    Card card;
    card.title = "keys";
    const Rgb text{150, 150, 160};
    card.lines = {
        keyed("+ -", "zoom", text),
        keyed("arrows", "move", text),
        keyed("/", "go to", text),
        keyed("f", "follow", text),
        keyed("r", "reset", text),
        keyed("g", "grid", text),
        keyed("n", "names", text),
        keyed("k", "key", text),
        keyed("q", "quit", text),
    };
    return card;
}

Card notes_card(const std::string& notes) {
    Card card;
    card.title = "notes";
    card.border_colour = {200, 150, 60};
    card.title_colour = {255, 190, 80};
    size_t start = 0;
    while (start < notes.size()) {                           // one line per note, in amber
        size_t end = notes.find('\n', start);
        if (end == std::string::npos) end = notes.size();
        if (end > start) card.lines.push_back(plain(notes.substr(start, end - start), {255, 190, 80}));
        start = end + 1;
    }
    return card;
}

Card legend_card() {
    Card card;
    card.title = "rain";
    std::string words((size_t)LEGEND_WIDTH, ' ');                  // "light rain" at one end, "heavy" at the other
    const std::string light = "light rain", heavy = "heavy";
    words.replace(0, light.size(), light);
    words.replace((size_t)LEGEND_WIDTH - heavy.size(), heavy.size(), heavy);
    card.lines.push_back(plain(words, {190, 190, 190}));
    for (const RainLook& look : legend_looks()) card.strip.push_back({look.brightness, {look.r, look.g, look.b}});
    return card;
}

Card driving_card(GpsState state, const GpsFix& fix, const std::string& problem, double age_seconds, const Car& car,
                  bool following) {
    Card card;
    card.title = following ? "driving, following" : "driving";
    if (state == GpsState::Off || state == GpsState::NoFile) return card;       // no lines: nothing to draw

    const Rgb grey{170, 170, 170}, bright{235, 235, 235}, amber{255, 190, 80};
    char text[120];
    std::string position;
    {
        std::snprintf(text, sizeof text, "%s %s", degrees_text(fix.lat, 'N', 'S').c_str(), degrees_text(fix.lon, 'E', 'W').c_str());
        position = text;
    }
    if (state == GpsState::Fix) {
        card.lines.push_back(plain(position, grey));
        std::snprintf(text, sizeof text, "%.0f mph", car.speed_mph());
        std::string speed = text;
        if (car.arrow()) {                                                       // a heading only while moving
            std::snprintf(text, sizeof text, "   heading %.0f° %s", car.heading(), compass_name(car.heading()));
            speed += text;
        }
        card.lines.push_back(plain(speed, bright));
        std::snprintf(text, sizeof text, "%ld ft   %d satellites", std::lround(fix.altitude_m * METERS_TO_FEET), fix.satellites);
        card.lines.push_back(plain(text, grey));
        return card;
    }

    // anything else is a warning, in the amber of the notes card
    card.border_colour = {200, 150, 60};
    card.title_colour = amber;
    if (state == GpsState::NoFix) {
        card.lines.push_back(plain("no fix yet", amber));
        std::snprintf(text, sizeof text, "%d satellites", fix.satellites);
        card.lines.push_back(plain(text, grey));
    } else if (state == GpsState::Stale) {
        std::snprintf(text, sizeof text, "lost, nothing for %d s", (int)std::max(0.0, age_seconds));
        card.lines.push_back(plain(text, amber));
        card.lines.push_back(plain("last known " + position, grey));
    } else {                                                                     // Bad
        card.lines.push_back(plain("unreadable", amber));
        card.lines.push_back(plain(problem, grey));
    }
    return card;
}

void draw_cards(vtscreen::Picture& picture, const View& view, const Settings& settings, const std::string& notes,
                const std::string& radar_status, bool show_help, bool show_notes, bool show_legend,
                const Card* driving) {
    const int rows_available = picture.bar_row() - CARDS_FREE_ROWS_AT_BOTTOM;
    int offset = 0;
    auto stack = [&](const Card& card) {
        const CardSize size = card_size(card, picture.cols);
        if (size.rows == 0 || offset + size.rows > rows_available) return;
        if (draw_card(picture, card, Corner::TopRight, offset)) offset += size.rows;
    };
    stack(view_card(view, settings, radar_status));
    if (show_notes && !notes.empty()) stack(notes_card(notes));      // a warning is never the card left out
    if (show_help) stack(help_card());
    if (show_legend && picture.cols >= LEGEND_MIN_COLS && picture.bar_row() >= LEGEND_MIN_ROWS)
        draw_card(picture, legend_card(), Corner::BottomRight, 0);
    if (driving != nullptr && !driving->lines.empty()) draw_card(picture, *driving, Corner::BottomLeft, 0);
}

}  // namespace vtw
