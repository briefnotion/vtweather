#include "prompt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace vtw {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trimmed(const std::string& s) {
    const size_t from = s.find_first_not_of(' ');
    if (from == std::string::npos) return "";
    return s.substr(from, s.find_last_not_of(' ') - from + 1);
}

std::string degrees_text(double value, char positive, char negative) {
    char text[24];
    std::snprintf(text, sizeof text, "%.1f%c", std::fabs(value), value >= 0 ? positive : negative);
    return text;
}

}  // namespace

std::vector<const MapPlace*> match_places(const BaseMap& map, const std::string& typed, size_t max) {
    const std::string wanted = lower(trimmed(typed));
    std::vector<const MapPlace*> starts, contains;
    if (wanted.empty()) return {};
    for (const MapPlace& place : map.places) {
        const size_t at = lower(place.name).find(wanted);
        if (at == 0) starts.push_back(&place);
        else if (at != std::string::npos) contains.push_back(&place);
    }
    auto bigger = [](const MapPlace* a, const MapPlace* b) { return a->population > b->population; };
    std::stable_sort(starts.begin(), starts.end(), bigger);
    std::stable_sort(contains.begin(), contains.end(), bigger);
    starts.insert(starts.end(), contains.begin(), contains.end());
    if (starts.size() > max) starts.resize(max);
    return starts;
}

PromptResult prompt_key(PromptState& prompt, const Key& key, const BaseMap& map, View& view) {
    switch (key.kind) {
        case KeyKind::Escape:
            prompt = PromptState();
            return PromptResult::Cancelled;
        case KeyKind::Backspace:
            if (!prompt.typed.empty()) prompt.typed.pop_back();
            prompt.selected = 0;
            return PromptResult::Stay;
        case KeyKind::Char:
            if (key.ch >= 32 && key.ch < 127 && prompt.typed.size() < MAX_TYPED) {
                prompt.typed += key.ch;
                prompt.selected = 0;
            }
            return PromptResult::Stay;
        case KeyKind::Up:
        case KeyKind::Down: {
            const int count = (int)match_places(map, prompt.typed).size();
            if (count > 0)
                prompt.selected = (prompt.selected + (key.kind == KeyKind::Down ? 1 : count - 1)) % count;
            return PromptResult::Stay;
        }
        case KeyKind::Enter: {
            const std::vector<const MapPlace*> found = match_places(map, prompt.typed);
            if (found.empty()) return PromptResult::Stay;
            const MapPlace* place = found[std::min<size_t>((size_t)prompt.selected, found.size() - 1)];
            view.set_centre(GeoPoint{place->lat / MAP_UNITS_PER_DEGREE, place->lon / MAP_UNITS_PER_DEGREE});
            if (view.zoom_km() > JUMP_ZOOM_LIMIT_KM) view.set_zoom_km(ZOOM_ON_JUMP_KM);
            prompt = PromptState();
            return PromptResult::Jumped;
        }
        default:
            return PromptResult::Stay;
    }
}

void draw_prompt(const PromptState& prompt, const BaseMap& map, vtscreen::Picture& picture) {
    if (!prompt.active) return;
    const int width = std::min(48, picture.cols);
    const int bottom = picture.bar_row() - 1;                        // the last row above the status bar
    auto line = [&](int row, std::string text, int r, int g, int b, bool inverse) {
        if (row < 0) return;
        if ((int)text.size() > width) text.resize((size_t)width);
        text.resize((size_t)width, ' ');                             // a solid block, so it reads over the map
        picture.text(row, 0, text, r, g, b, inverse);
    };

    const std::vector<const MapPlace*> found = match_places(map, prompt.typed);
    line(bottom, " go to: " + prompt.typed + "_", 255, 235, 150, false);
    int row = bottom - 1;
    if (found.empty()) {
        line(row, prompt.typed.empty() ? " type a city name, Enter to go, Esc to cancel" : " no city like that", 170, 170, 170, false);
        return;
    }
    // the best match nearest the typing line, the rest above it
    for (size_t i = 0; i < found.size(); i++, row--) {
        const MapPlace& p = *found[i];
        char where[40];
        std::snprintf(where, sizeof where, "  %s %s", degrees_text(p.lat / MAP_UNITS_PER_DEGREE, 'N', 'S').c_str(),
                      degrees_text(p.lon / MAP_UNITS_PER_DEGREE, 'E', 'W').c_str());
        line(row, " " + p.name + where, 225, 225, 225, (int)i == prompt.selected);
    }
}

}  // namespace vtw
