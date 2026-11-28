// The pen map: the map's lines (the coast and shores, country and state borders, rivers, highways and other
// roads) drawn as thin lines with the pen (vtpen), over the letters, instead of as letters. They are described as
// one vtpen Drawing the size of the map in pixels and put on the Picture (Picture::place_drawing); vtscreen's Display
// draws them as ReGIS lines, or as pixels on a terminal that does Sixel only.
//
// Lines never run through text: every cell the Picture has a letter of the scene's own in (a card, a city name, the
// prompt, the car) is kept clear, and a line is cut where it would cross one. The lines are made again only when the
// view, the size or those cells change, and no more than ten times a second, so a map that is moving is not sent
// faster than that.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "basemap.h"
#include "cell_point.h"
#include "radar_frame.h"
#include "rain_contours.h"
#include "view.h"
#include "vtpen_drawing.h"
#include "vtscreen/vtscreen.h"

namespace vtw {

// Cells no line may pass through: cols * rows bytes, row by row, 1 = blocked.
using Blocked = std::vector<uint8_t>;

// The cells of a picture that have text in them (a letter the scene chose), in the map's rows (the status bar is not one).
Blocked blocked_cells(const vtscreen::Picture& picture, int cols, int rows);

// Cut a line (a list of positions in cells) down to the parts that are inside the cols x rows cells and not in a blocked
// cell. The parts come back as lines; one that is cut only at its ends is one line. Positions that are not numbers
// end the line there.
void clip_to_cells(const std::vector<CellPoint>& line, int cols, int rows, const Blocked& blocked,
                   std::vector<std::vector<CellPoint>>& pieces);

// How each kind of line looks, as the same colours the letter map uses.
struct PenStyle {
    uint8_t r = 0, g = 0, b = 0;
    double thickness = 1;
};
PenStyle pen_style(LayerKind kind);
PenStyle pen_style_coast();

// The lines of the map for a view, as a Drawing cols * cell_w by rows * cell_h pixels big (the view's picture area).
// Which lines, and from which level of detail, follows the zoom the way the letter map's does. Null if the view or
// the cell size is nothing. white = true draws every line white, for a screen without colour (--no-color). rain, if given, is
// drawn first as contour lines (see rain_pen.h), so the map's lines are on top of it.
std::shared_ptr<const vtpen::Drawing> make_pen_map(const BaseMap& map, const View& view, int cell_w, int cell_h,
                                                   const Blocked& blocked, bool white = false, const RainContours* rain = nullptr);

// Keeps the drawing for the current view, made again when something changed (and not too often), and puts it on the picture.
class PenMap {
public:
    explicit PenMap(const BaseMap& map) : map_(map) {}

    // Call once a frame, after everything with text in it is on the picture. If the pen map is on, the drawing goes on the
    // picture. now is seconds; cell_w and cell_h are the terminal's character cell in pixels; white as for make_pen_map. radar, if
    // given (not null and not empty), is drawn as contour lines under the map's lines; radar_id changes whenever it is another picture.
    void update(const View& view, vtscreen::Picture& picture, int cell_w, int cell_h, double now, bool on, bool white = false,
                const RadarFrame* radar = nullptr, uint64_t radar_id = 0);

    // How many drawings, and how many sets of the rain's contour lines, have been made (for the tests).
    int made() const { return made_; }
    int contours_made() const { return contours_made_; }
    std::shared_ptr<const vtpen::Drawing> drawing() const { return drawing_; }

private:
    const BaseMap& map_;
    std::shared_ptr<const vtpen::Drawing> drawing_;
    bool have_ = false, white_ = false, have_rain_ = false;
    uint64_t radar_id_ = 0;
    RainContours contours_;                      // for the view and picture it was made for (reused while only the text cells change)
    bool have_contours_ = false;
    uint64_t contours_radar_id_ = 0;
    int contours_cols_ = 0, contours_rows_ = 0;
    GeoPoint contours_centre_;
    double contours_zoom_ = 0, contours_aspect_ = 0, contours_heading_ = 0;
    int cols_ = 0, rows_ = 0, cell_w_ = 0, cell_h_ = 0;
    GeoPoint centre_;
    double zoom_ = 0, aspect_ = 0, heading_ = 0;
    Blocked blocked_;
    double made_at_ = -1e9;
    int made_ = 0, contours_made_ = 0;
};

constexpr double PEN_MAP_MIN_SECONDS = 0.1;       // between two drawings, while the view keeps changing

}  // namespace vtw
