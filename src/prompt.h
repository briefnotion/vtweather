// The go-to prompt: press / and type the name of a city; the cities that match are listed (the
// biggest first), Up and Down choose one, Enter moves the view there and Escape gives up. The names
// come from the base map's own list of cities, so it works offline and asks nobody anything.
#pragma once

#include <string>
#include <vector>

#include "basemap.h"
#include "keys.h"
#include "view.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

constexpr size_t MAX_SUGGESTIONS = 6;
constexpr size_t MAX_TYPED = 24;
constexpr double ZOOM_ON_JUMP_KM = 150;          // a jump from further out than this zooms in this far
constexpr double JUMP_ZOOM_LIMIT_KM = 400;

struct PromptState {
    bool active = false;
    std::string typed;
    int selected = 0;
};

// The cities whose names match what was typed (ignoring case): those that start with it first, then
// those that contain it, the biggest first within each. At most max are returned.
std::vector<const MapPlace*> match_places(const BaseMap& map, const std::string& typed, size_t max = MAX_SUGGESTIONS);

enum class PromptResult { Stay, Cancelled, Jumped };

// A key pressed while the prompt is open. Enter on a match moves the view to it (and closes the
// prompt); Escape closes it.
PromptResult prompt_key(PromptState& prompt, const Key& key, const BaseMap& map, View& view);

// Draw the prompt over the lower left of the picture: the matches, and the line being typed.
void draw_prompt(const PromptState& prompt, const BaseMap& map, vtscreen::Picture& picture);

}  // namespace vtw
