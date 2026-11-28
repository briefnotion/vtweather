#include "pen_support.h"

#include <algorithm>

#include "vtscreen/vtscreen.h"

#if defined(VTSCREEN_VECTORS) && !defined(_WIN32) && __has_include("vtpen_probe_posix.h")
#include <unistd.h>

#include "vtpen_probe_posix.h"
#define VTW_CAN_PROBE 1
#endif

namespace vtw {

PenSupport pen_support_from(const vtpen::Capabilities& cap, int map_cols, int map_rows) {
    PenSupport support;
    if (!cap.answered || cap.cell_w <= 0 || cap.cell_h <= 0) return support;
    if (cap.regis) {
        PenSupport regis;
        regis.kind = PenKind::Regis;
        regis.cell_w = cap.cell_w; regis.cell_h = cap.cell_h;
        regis.surface_w = cap.max_width > 0 ? cap.max_width : 800;
        regis.surface_h = cap.max_height > 0 ? cap.max_height : 480;
        if (cap.window_w > 0) regis.surface_w = std::min(regis.surface_w, cap.window_w);
        if (cap.window_h > 0) regis.surface_h = std::min(regis.surface_h, cap.window_h);
        if (pen_covers(regis, map_cols, map_rows)) return regis;
    }
    if (cap.sixel) {
        support.kind = PenKind::Sixel;
        support.cell_w = cap.cell_w; support.cell_h = cap.cell_h;
    }
    return support;
}

bool pen_covers(const PenSupport& support, int map_cols, int map_rows) {
    switch (support.kind) {
        case PenKind::Sixel: return true;
        case PenKind::Regis:
            return (long long)map_cols * support.cell_w <= support.surface_w && (long long)map_rows * support.cell_h <= support.surface_h;
        default: return false;
    }
}

PenSupport detect_pen_support(int map_cols, int map_rows) {
#ifdef VTW_CAN_PROBE
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return PenSupport();
    return pen_support_from(vtpen::probe_terminal(STDIN_FILENO, STDOUT_FILENO, 1000), map_cols, map_rows);
#else
    (void)map_cols; (void)map_rows;
    return PenSupport();
#endif
}

void apply_pen_support(const PenSupport& support) {
#ifdef VTSCREEN_VECTORS
    vtscreen::set_image_support(0, 0);
    vtscreen::set_regis_support(0, 0, 0, 0);
    if (support.kind == PenKind::Sixel) vtscreen::set_image_support(support.cell_w, support.cell_h);
    if (support.kind == PenKind::Regis) vtscreen::set_regis_support(support.cell_w, support.cell_h, support.surface_w, support.surface_h);
#else
    (void)support;
#endif
}

}  // namespace vtw
