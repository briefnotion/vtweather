#include "card.h"

#include <algorithm>

namespace vtw {

namespace {

constexpr int WRAP_INDENT = 2;             // the second and later rows of a wrapped line start this far in

struct Row {
    std::string key;
    std::string text;
    Rgb key_colour, text_colour;
};

struct Layout {
    std::vector<Row> rows;
    std::string title;
    int key_width = 0;
    int content_width = 0;                 // the text area, without the frame and its padding
    int width = 0;                         // the whole card
    int height = 0;
    int strip_cells = 0;                   // how many cells of the strip are drawn (0: no strip row)
};

// Break a line at spaces so that no piece is longer than max cells; the pieces after the first are indented.
std::vector<std::string> wrap(const std::string& text, int max) {
    std::vector<std::string> pieces;
    std::string rest = text;
    bool first = true;
    for (;;) {
        const int limit = first ? max : max - WRAP_INDENT;
        if (utf8_cells(rest) <= limit) break;
        const std::string head = utf8_cut(rest, limit);
        size_t cut = head.rfind(' ');
        size_t resume;
        if (cut == std::string::npos || cut == 0) { cut = head.size(); resume = cut; }    // no space: cut hard
        else { resume = cut + 1; }
        pieces.push_back((first ? "" : std::string(WRAP_INDENT, ' ')) + head.substr(0, cut));
        rest = rest.substr(resume);
        first = false;
    }
    pieces.push_back((first ? "" : std::string(WRAP_INDENT, ' ')) + rest);
    return pieces;
}

Layout lay_out(const Card& card, int picture_cols) {
    Layout layout;
    const int widest = picture_cols - 2 * CARD_MARGIN;              // the card with its frame
    const int max_text = std::min(CARD_MAX_TEXT, widest - 4);
    if (max_text < 4) return layout;                                 // nothing useful fits

    for (const CardLine& line : card.lines)
        layout.key_width = std::max(layout.key_width, utf8_cells(line.key));
    layout.key_width = std::min(layout.key_width, max_text / 2);
    const int key_gap = layout.key_width > 0 ? 1 : 0;
    const int text_room = max_text - layout.key_width - key_gap;

    for (const CardLine& line : card.lines) {
        const std::string key = utf8_cut(line.key, layout.key_width);
        if (line.key.empty()) {
            for (const std::string& piece : wrap(line.text, text_room))
                layout.rows.push_back({"", piece, line.key_colour, line.text_colour});
        } else {
            layout.rows.push_back({key, utf8_cut(line.text, text_room), line.key_colour, line.text_colour});
        }
    }
    for (const Row& row : layout.rows)
        layout.content_width = std::max(layout.content_width, layout.key_width + key_gap + utf8_cells(row.text));
    layout.content_width = std::max(layout.content_width, std::min((int)card.strip.size(), max_text));
    layout.strip_cells = std::min((int)card.strip.size(), max_text);

    // the title sits in the top edge between "╭─┤ " and " ├" and needs a bar of line after it
    layout.title = utf8_cut(card.title, std::min(CARD_MAX_TEXT, widest - 8));
    const int title_cells = utf8_cells(layout.title);
    layout.width = layout.content_width + 4;
    if (title_cells > 0) layout.width = std::max(layout.width, title_cells + 8);
    layout.width = std::max(layout.width, 6);
    layout.height = (int)layout.rows.size() + (layout.strip_cells > 0 ? 1 : 0) + 2;
    return layout;
}

// Put text on the picture with the card's background behind it.
void put(vtscreen::Picture& picture, int row, int col, const std::string& text, Rgb colour, Rgb background) {
    picture.text(row, col, text, colour.r, colour.g, colour.b);
    const int cells = utf8_cells(text);
    for (int j = col; j < col + cells; j++) {
        if (j < 0 || j >= picture.cols || row < 0 || row >= picture.rows) continue;
        vtscreen::PicCell& cell = picture.at(row, j);
        cell.alt_lum = 1;
        cell.alt_r = background.r; cell.alt_g = background.g; cell.alt_b = background.b;
    }
}

std::string repeated(const std::string& glyph, int n) {
    std::string s;
    for (int i = 0; i < n; i++) s += glyph;
    return s;
}

}  // namespace

int utf8_cells(const std::string& s) {
    int cells = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) cells++;                             // not a continuation byte
    return cells;
}

std::string utf8_cut(const std::string& s, int cells) {
    if (cells <= 0) return "";
    int seen = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (((unsigned char)s[i] & 0xC0) == 0x80) continue;
        if (seen == cells) return s.substr(0, i);
        seen++;
    }
    return s;
}

CardSize card_size(const Card& card, int picture_cols) {
    const Layout layout = lay_out(card, picture_cols);
    return {layout.width, layout.height};
}

bool draw_card(vtscreen::Picture& picture, const Card& card, Corner corner, int row_offset) {
    const Layout layout = lay_out(card, picture.cols);
    if (layout.width == 0) return false;
    const bool right = corner == Corner::TopRight || corner == Corner::BottomRight;
    const bool bottom = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    const int left = right ? picture.cols - CARD_MARGIN - layout.width : CARD_MARGIN;
    const int top = bottom ? picture.bar_row() - row_offset - layout.height : row_offset;
    if (left < 0 || top < 0 || top + layout.height > picture.bar_row()) return false;

    const Rgb line = card.border_colour, bg = card.background;
    const int inner = layout.width - 2;

    // the top edge, with the title set into it
    const int title_cells = utf8_cells(layout.title);
    if (title_cells > 0) {
        put(picture, top, left, "╭─┤ ", line, bg);
        put(picture, top, left + 4, layout.title, card.title_colour, bg);
        put(picture, top, left + 4 + title_cells, " ├", line, bg);
        const int used = 6 + title_cells;
        put(picture, top, left + used, repeated("─", layout.width - used - 1) + "╮", line, bg);
    } else {
        put(picture, top, left, "╭" + repeated("─", inner) + "╮", line, bg);
    }

    // the lines of text
    for (size_t i = 0; i < layout.rows.size(); i++) {
        const Row& row = layout.rows[i];
        const int y = top + 1 + (int)i;
        put(picture, y, left, "│" + std::string((size_t)inner, ' ') + "│", line, bg);
        put(picture, y, left + 2, row.key, row.key_colour, bg);
        put(picture, y, left + 2 + layout.key_width + (layout.key_width > 0 ? 1 : 0), row.text, row.text_colour, bg);
    }

    // the strip: cells of the map's own kind, with the card's background behind the letters
    if (layout.strip_cells > 0) {
        const int y = top + 1 + (int)layout.rows.size();
        put(picture, y, left, "│" + std::string((size_t)inner, ' ') + "│", line, bg);
        for (int i = 0; i < layout.strip_cells; i++) {
            const Swatch& swatch = card.strip[(size_t)i];
            vtscreen::PicCell& cell = picture.at(y, left + 2 + i);
            cell = vtscreen::PicCell();
            cell.lum = swatch.brightness;
            cell.r = swatch.colour.r; cell.g = swatch.colour.g; cell.b = swatch.colour.b;
            cell.shimmer = 0;
            cell.alt_lum = 1;
            cell.alt_r = bg.r; cell.alt_g = bg.g; cell.alt_b = bg.b;
        }
    }

    put(picture, top + layout.height - 1, left, "╰" + repeated("─", inner) + "╯", line, bg);
    return true;
}

}  // namespace vtw
