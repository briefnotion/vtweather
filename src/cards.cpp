#include "cards.h"

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

Card view_card(const View& view, const Settings& settings, const std::string& radar_status, const std::string& gps_status) {
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
    if (!gps_status.empty()) card.lines.push_back(plain(gps_status, {150, 190, 230}));
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

void draw_cards(vtscreen::Picture& picture, const View& view, const Settings& settings, const std::string& notes,
                const std::string& radar_status, bool show_help, bool show_notes, bool show_legend,
                const std::string& gps_status) {
    const int rows_available = picture.bar_row() - CARDS_FREE_ROWS_AT_BOTTOM;
    int offset = 0;
    auto stack = [&](const Card& card) {
        const CardSize size = card_size(card, picture.cols);
        if (size.rows == 0 || offset + size.rows > rows_available) return;
        if (draw_card(picture, card, Corner::TopRight, offset)) offset += size.rows;
    };
    stack(view_card(view, settings, radar_status, gps_status));
    if (show_notes && !notes.empty()) stack(notes_card(notes));      // a warning is never the card left out
    if (show_help) stack(help_card());
    if (show_legend && picture.cols >= LEGEND_MIN_COLS && picture.bar_row() >= LEGEND_MIN_ROWS)
        draw_card(picture, legend_card(), Corner::BottomRight, 0);
}

}  // namespace vtw
