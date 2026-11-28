// Input: what the keys do. Every key the program cares about becomes a Command, and a
// command is applied to the View in one place, so keys and (later) prompts and other inputs
// all go through the same door. A key press zooms by a fixed step or pans by a tenth of the
// screen; holding a key repeats it, which gives smooth movement.
//
//   +  =        zoom in            -  _        zoom out
//   w a s d, or the arrow keys: move up, left, down, right (w looks further north)
//   /  or  l    go to a city: type its name, Up and Down choose, Enter goes, Escape gives up
//   f           follow the car: the map stays centred on it (see car.h). Any move of your own (the move keys, r,
//               going to a city) stops following; f starts it again, and the map jumps back to the car
//   h           heading up: turn the map so the car's direction of travel is up, while following and moving (see
//               orientation.h); north up otherwise. North up is the default
//   r           back to the place and zoom in the settings
//   g           show or hide the grid of latitude and longitude lines
//   n           show or hide the city names
//   k           show or hide the key to the rain
//   p           the map's lines (coast, borders, rivers, roads) drawn with the pen, or as letters (see pen_map.h)
#pragma once

#include "keys.h"
#include "prompt.h"
#include "settings.h"
#include "view.h"

namespace vtw {

enum class Command { None, ZoomIn, ZoomOut, PanLeft, PanRight, PanUp, PanDown, Reset, ToggleGraticule, ToggleNames, ToggleLegend, OpenGoto, ToggleFollow, ToggleHeadingUp, TogglePen };

// What is shown, apart from where the view is.
struct UiState {
    bool graticule = false;
    bool names = true;
    bool legend = true;
    bool follow = false;               // the map follows the car (f); not remembered between runs
    bool heading_up = false;           // the map turns so the car's direction is up (h), when following and moving
    bool pen = true;                   // the map's lines drawn with the pen (p), when the terminal can show it; else letters
    PromptState prompt;                // the go-to prompt, when it is open
};

constexpr double ZOOM_STEP = 0.8;          // one press shows 80 % of the distance (zooms in)
constexpr double PAN_STEP = 0.10;          // one press moves a tenth of the screen

Command command_for_key(char key);
Command command_for_event(const Key& key);      // a key as the KeyReader gives it: arrows too

void apply_command(Command command, View& view, const Settings& settings, UiState& ui);

// What the go-to prompt did: jumping to a city is a move of your own, so it stops following.
void apply_prompt_result(PromptResult result, UiState& ui);

}  // namespace vtw
