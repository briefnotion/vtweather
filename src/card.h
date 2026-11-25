// Card: a small framed window of text drawn over the map, in one of the corners. The frame is a
// thin line with rounded corners and the title set into the top edge, like this:
//
//     ╭─┤ keys ├──╮
//     │ + -   zoom │
//     ╰────────────╯
//
// The inside has a dim background so the text reads over the map. A card is plain data (a title and
// lines of text); draw_card puts it on the picture as a layer, the way the other draw_ routines do.
// What goes on each card, and where the cards are stacked, is in cards.h.
//
// A card may also have a strip, a row of colour cells under its lines (the key to the rain uses it).
//
// Lines are plain text. A line may have a key (a short left column, such as the key to press) and a
// text; the texts line up after the widest key. A line without a key that is longer than
// CARD_MAX_TEXT is wrapped at spaces. With --no-color the frame and the text are still there; only
// the colours and the background are not.
#pragma once

#include <string>
#include <vector>

#include "vtscreen/vtscreen.h"

namespace vtw {

struct Rgb { int r, g, b; };

struct CardLine {
    std::string key;                       // the left column, optional
    std::string text;
    Rgb key_colour{235, 235, 235};
    Rgb text_colour{170, 170, 170};
};

// One cell of a strip: a cell of the map's own kind (brightness and colour, so it shows the letter and
// colour it would on the map; it holds still).
struct Swatch {
    double brightness;
    Rgb colour;
};

struct Card {
    std::string title;
    std::vector<CardLine> lines;
    std::vector<Swatch> strip;             // optional: one row of cells under the lines, one cell per swatch
    Rgb border_colour{110, 125, 150};
    Rgb title_colour{225, 225, 225};
    Rgb background{10, 14, 22};
};

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

constexpr int CARD_MAX_TEXT = 44;          // the widest a line of text is allowed to be, in cells
constexpr int CARD_MARGIN = 1;             // the empty cells between a card and the side of the screen

struct CardSize {
    int cols = 0;                          // with the frame
    int rows = 0;
};

// How big the card is when drawn on a picture this many cells wide (long lines wrap, and a wide
// title is cut, to fit; a picture too narrow for anything useful gets 0 by 0).
CardSize card_size(const Card& card, int picture_cols);

// Draw the card in a corner, row_offset rows in from the top (for the top corners) or up from the
// last row above the status bar (for the bottom corners). That is how cards are stacked: the next
// one starts at the offset plus the height of this one. Draws nothing and returns false if the card
// does not fit on the picture.
bool draw_card(vtscreen::Picture& picture, const Card& card, Corner corner, int row_offset);

// How many cells a UTF-8 string takes, and the first n of them (for the tests and the wrap).
int utf8_cells(const std::string& s);
std::string utf8_cut(const std::string& s, int cells);

}  // namespace vtw
