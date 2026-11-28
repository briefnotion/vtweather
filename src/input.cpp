#include "input.h"

namespace vtw {

Command command_for_key(char key) {
    switch (key) {
        case '+': case '=': return Command::ZoomIn;
        case '-': case '_': return Command::ZoomOut;
        case 'a': case 'A': return Command::PanLeft;
        case 'd': case 'D': return Command::PanRight;
        case 'w': case 'W': return Command::PanUp;
        case 's': case 'S': return Command::PanDown;
        case 'r': case 'R': return Command::Reset;
        case 'g': case 'G': return Command::ToggleGraticule;
        case 'n': case 'N': return Command::ToggleNames;
        case 'k': case 'K': return Command::ToggleLegend;
        case 'f': case 'F': return Command::ToggleFollow;
        case 'h': case 'H': return Command::ToggleHeadingUp;
        case 'p': case 'P': return Command::TogglePen;
        case '/': case 'l': case 'L': return Command::OpenGoto;
        default: return Command::None;
    }
}

Command command_for_event(const Key& key) {
    switch (key.kind) {
        case KeyKind::Char: return command_for_key(key.ch);
        case KeyKind::Up: return Command::PanUp;
        case KeyKind::Down: return Command::PanDown;
        case KeyKind::Left: return Command::PanLeft;
        case KeyKind::Right: return Command::PanRight;
        default: return Command::None;
    }
}

void apply_command(Command command, View& view, const Settings& settings, UiState& ui) {
    switch (command) {
        case Command::ZoomIn: view.zoom(ZOOM_STEP); break;
        case Command::ZoomOut: view.zoom(1 / ZOOM_STEP); break;
        case Command::PanLeft: view.pan_cells(-PAN_STEP * view.cols(), 0); ui.follow = false; break;
        case Command::PanRight: view.pan_cells(PAN_STEP * view.cols(), 0); ui.follow = false; break;
        case Command::PanUp: view.pan_cells(0, -PAN_STEP * view.rows()); ui.follow = false; break;
        case Command::PanDown: view.pan_cells(0, PAN_STEP * view.rows()); ui.follow = false; break;
        case Command::Reset:
            view.set_centre(settings.home);
            view.set_zoom_km(settings.zoom_km);
            ui.follow = false;
            break;
        case Command::ToggleGraticule: ui.graticule = !ui.graticule; break;
        case Command::ToggleNames: ui.names = !ui.names; break;
        case Command::ToggleLegend: ui.legend = !ui.legend; break;
        case Command::OpenGoto: ui.prompt = PromptState(); ui.prompt.active = true; break;
        case Command::ToggleFollow: ui.follow = !ui.follow; break;
        case Command::ToggleHeadingUp: ui.heading_up = !ui.heading_up; break;
        case Command::TogglePen: ui.pen = !ui.pen; break;
        case Command::None: break;
    }
}

void apply_prompt_result(PromptResult result, UiState& ui) {
    if (result == PromptResult::Jumped) ui.follow = false;
}

}  // namespace vtw
