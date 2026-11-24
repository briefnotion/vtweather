#include "draw_places.h"

namespace vtw {

void draw_places(const PlaceLabels& places, vtscreen::Picture& picture) {
    for (const PlaceLabel& label : places.labels()) {
        int level = 170;                                     // small towns: grey
        if (label.population >= 1000000) level = 240;        // big cities: white
        else if (label.population >= 150000) level = 205;
        const int r = level, g = level, b = level > 200 ? level - 10 : level - 15;
        picture.text(label.row, label.col, "\xE2\x80\xA2", r, g, b);       // a dot
        picture.text(label.row, label.text_col, label.text, r, g, b);
    }
}

}  // namespace vtw
