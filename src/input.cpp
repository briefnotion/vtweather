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
        default: return Command::None;
    }
}

void apply_command(Command command, View& view, const Settings& settings) {
    switch (command) {
        case Command::ZoomIn: view.zoom(ZOOM_STEP); break;
        case Command::ZoomOut: view.zoom(1 / ZOOM_STEP); break;
        case Command::PanLeft: view.pan_cells(-PAN_STEP * view.cols(), 0); break;
        case Command::PanRight: view.pan_cells(PAN_STEP * view.cols(), 0); break;
        case Command::PanUp: view.pan_cells(0, -PAN_STEP * view.rows()); break;
        case Command::PanDown: view.pan_cells(0, PAN_STEP * view.rows()); break;
        case Command::Reset:
            view.set_centre(settings.home);
            view.set_zoom_km(settings.zoom_km);
            break;
        case Command::None: break;
    }
}

}  // namespace vtw
