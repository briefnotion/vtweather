// Input: what the keys do. Every key the program cares about becomes a Command, and a
// command is applied to the View in one place, so keys and (later) prompts and other inputs
// all go through the same door. A key press zooms by a fixed step or pans by a tenth of the
// screen; holding a key repeats it, which gives smooth movement.
//
//   +  =        zoom in            -  _        zoom out
//   w a s d, or the arrow keys: move up, left, down, right (w looks further north)
//   /  or  l    go to a city: type its name, Up and Down choose, Enter goes, Escape gives up
//   r           back to the place and zoom in the settings
//   g           show or hide the grid of latitude and longitude lines
//   n           show or hide the city names
#pragma once

#include "keys.h"
#include "prompt.h"
#include "settings.h"
#include "view.h"

namespace vtw {

enum class Command { None, ZoomIn, ZoomOut, PanLeft, PanRight, PanUp, PanDown, Reset, ToggleGraticule, ToggleNames, OpenGoto };

// What is shown, apart from where the view is.
struct UiState {
    bool graticule = false;
    bool names = true;
    PromptState prompt;                // the go-to prompt, when it is open
};

constexpr double ZOOM_STEP = 0.8;          // one press shows 80 % of the distance (zooms in)
constexpr double PAN_STEP = 0.10;          // one press moves a tenth of the screen

Command command_for_key(char key);
Command command_for_event(const Key& key);      // a key as the KeyReader gives it: arrows too

void apply_command(Command command, View& view, const Settings& settings, UiState& ui);

}  // namespace vtw
