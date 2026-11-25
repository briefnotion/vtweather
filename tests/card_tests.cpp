// Tests for the cards: the frame and its size, wrapping, where a card sits in each corner, stacking,
// what each card says, and when the keys card and the notes card come and go.
#include <string>

#include "src/cards.h"
#include "check.h"

using namespace vtw;

namespace {

// The character in a cell (a space for a cell that is not text).
char32_t at(const vtscreen::Picture& p, int row, int col) {
    const vtscreen::PicCell& c = p.at(row, col);
    return c.text ? c.ch : U' ';
}

// A row of the picture as UTF-32, from col for n cells.
std::u32string row_text(const vtscreen::Picture& p, int row, int col, int n) {
    std::u32string s;
    for (int j = col; j < col + n; j++) s += at(p, row, j);
    return s;
}

// Does the u32 string s contain the ASCII text t?
bool has(const std::u32string& s, const std::string& t) {
    std::u32string u(t.begin(), t.end());
    return s.find(u) != std::u32string::npos;
}

bool picture_is_blank(const vtscreen::Picture& p) {
    for (int i = 0; i < p.rows; i++)
        for (int j = 0; j < p.cols; j++)
            if (p.at(i, j).text) return false;
    return true;
}

Card small_card() {
    Card card;
    card.title = "keys";
    card.lines = {{"", "one", {}, {}}, {"", "three", {}, {}}};
    return card;
}

}  // namespace

int main() {
    // ---- UTF-8 helpers
    CHECK(utf8_cells("abc") == 3);
    CHECK(utf8_cells("") == 0);
    CHECK(utf8_cells("é─x") == 3);                       // two-byte and three-byte letters are one cell each
    CHECK(utf8_cut("é─xyz", 2) == "é─");
    CHECK(utf8_cut("abc", 5) == "abc");
    CHECK(utf8_cut("abc", 0) == "");

    // ---- size: the longest line plus the frame and one cell of padding each side; lines plus top and bottom
    {
        const CardSize size = card_size(small_card(), 100);
        CHECK(size.rows == 4);
        CHECK(size.cols == 5 + 4 || size.cols == 4 + 8);              // "three" is 5; the title "keys" needs 4 + 8
        CHECK(size.cols == 12);                                       // the title is the wider: 4 + 8
        Card wide = small_card();
        wide.lines.push_back({"", "a line of some twenty-six", {}, {}});
        CHECK(card_size(wide, 100).cols == 25 + 4);
        Card untitled = wide;
        untitled.title.clear();
        CHECK(card_size(untitled, 100).cols == 25 + 4);
        Card empty;
        CHECK(card_size(empty, 100).rows == 2);                       // just the frame
        CHECK(card_size(empty, 100).cols == 6);
    }

    // ---- keys line up in a column
    {
        Card card;
        card.title = "keys";
        card.lines = {{"+ -", "zoom", {}, {}}, {"arrows", "move", {}, {}}};
        const CardSize size = card_size(card, 100);
        CHECK(size.cols == 6 + 1 + 4 + 4);                            // key column 6, a gap, "zoom", frame and padding
        vtscreen::Picture p;
        p.init(20, 100);
        CHECK(draw_card(p, card, Corner::TopRight, 0));
        const int left = 100 - CARD_MARGIN - size.cols;
        CHECK(row_text(p, 1, left, size.cols) == U"│ + -    zoom │");
        CHECK(row_text(p, 2, left, size.cols) == U"│ arrows move │");
    }

    // ---- wrapping: no line wider than the limit, nothing lost
    {
        Card card;
        card.title = "radar";
        const std::string long_line = "radar: scan 02:58 (08:58Z), 5 min old; update failed (could not connect), next try in 30 s";
        card.lines = {{"", long_line, {}, {}}};
        const CardSize size = card_size(card, 100);
        CHECK(size.rows > 3);                                         // it took more than one line
        CHECK(size.cols <= CARD_MAX_TEXT + 4);
        vtscreen::Picture p;
        p.init(20, 100);
        CHECK(draw_card(p, card, Corner::TopRight, 0));
        const int left = 100 - CARD_MARGIN - size.cols;
        std::u32string all;
        for (int r = 1; r < size.rows - 1; r++) {
            std::u32string inner = row_text(p, r, left + 2, size.cols - 4);
            while (!inner.empty() && inner.back() == U' ') inner.pop_back();
            if (r > 1) CHECK(inner.size() > 2 && inner[0] == U' ' && inner[1] == U' ');   // continuation lines are indented
            all += inner + U" ";
        }
        for (const char* word : {"02:58", "08:58Z", "failed", "connect", "30"}) CHECK(has(all, word));
        // a word with no space in it is cut hard rather than overflowing
        Card hard;
        hard.lines = {{"", std::string(100, 'x'), {}, {}}};
        CHECK(card_size(hard, 100).cols <= CARD_MAX_TEXT + 4);
    }

    // ---- the frame, in each corner
    {
        const Card card = small_card();
        const CardSize size = card_size(card, 100);
        struct Case { Corner corner; int left; int top; } cases[] = {
            {Corner::TopLeft, CARD_MARGIN, 0},
            {Corner::TopRight, 100 - CARD_MARGIN - size.cols, 0},
            {Corner::BottomLeft, CARD_MARGIN, 29 - size.rows},        // the bar row is 29 on a 30-row picture
            {Corner::BottomRight, 100 - CARD_MARGIN - size.cols, 29 - size.rows},
        };
        for (const Case& c : cases) {
            vtscreen::Picture p;
            p.init(29, 100);                                          // 29 rows and a status bar: bar_row() is 29
            CHECK(p.bar_row() == 29);
            CHECK(draw_card(p, card, c.corner, 0));
            CHECK(at(p, c.top, c.left) == U'╭');
            CHECK(at(p, c.top, c.left + size.cols - 1) == U'╮');
            CHECK(at(p, c.top + size.rows - 1, c.left) == U'╰');
            CHECK(at(p, c.top + size.rows - 1, c.left + size.cols - 1) == U'╯');
            CHECK(at(p, c.top + 1, c.left) == U'│');
            CHECK(at(p, c.top + 1, c.left + size.cols - 1) == U'│');
            CHECK(has(row_text(p, c.top, c.left, size.cols), "keys"));
            CHECK(row_text(p, c.top, c.left, 4) == U"╭─┤ ");           // the title is set into the top edge
            CHECK(row_text(p, c.top, c.left + 8, 2) == U" ├");
            CHECK(has(row_text(p, c.top + 1, c.left, size.cols), "one"));
            CHECK(has(row_text(p, c.top + 2, c.left, size.cols), "three"));
            // every cell inside the box has the card's background; the cells just outside do not
            bool all_background = true;
            for (int i = 0; i < size.rows; i++)
                for (int j = 0; j < size.cols; j++)
                    if (!(p.at(c.top + i, c.left + j).alt_lum > 0)) all_background = false;
            CHECK(all_background);
            CHECK(p.at(c.top, c.left - 1).alt_lum == 0);
            CHECK(!p.at(c.top, c.left - 1).text);
        }
    }

    // ---- stacking by offset, and a card that does not fit draws nothing
    {
        vtscreen::Picture p;
        p.init(20, 100);
        const CardSize size = card_size(small_card(), 100);
        CHECK(draw_card(p, small_card(), Corner::TopRight, 0));
        CHECK(draw_card(p, small_card(), Corner::TopRight, size.rows));
        const int left = 100 - CARD_MARGIN - size.cols;
        CHECK(at(p, size.rows, left) == U'╭');                        // the second begins under the first
        CHECK(at(p, size.rows - 1, left) == U'╰');

        vtscreen::Picture q;
        q.init(20, 100);
        CHECK(!draw_card(q, small_card(), Corner::TopRight, 18));     // 18 + 4 rows: past the status bar
        CHECK(picture_is_blank(q));
        CHECK(!draw_card(q, small_card(), Corner::BottomLeft, 18));
        CHECK(picture_is_blank(q));
        CHECK(!draw_card(q, small_card(), Corner::TopRight, -1));
        CHECK(picture_is_blank(q));

        vtscreen::Picture narrow;
        narrow.init(20, 8);                                           // too narrow to hold anything useful
        CHECK(!draw_card(narrow, small_card(), Corner::TopRight, 0));
        CHECK(picture_is_blank(narrow));
        CHECK(card_size(small_card(), 8).cols == 0);
        vtscreen::Picture snug;
        snug.init(20, 20);                                            // narrow: the card is made to fit
        CHECK(draw_card(snug, small_card(), Corner::TopRight, 0));
        CHECK(at(snug, 0, 19 - CARD_MARGIN) == U'╮');
    }

    // ---- a wide title is cut to fit, and is still inside the frame
    {
        Card card;
        card.title = std::string(80, 'T');
        card.lines = {{"", "x", {}, {}}};
        const CardSize size = card_size(card, 100);
        CHECK(size.cols <= CARD_MAX_TEXT + 8);
        vtscreen::Picture p;
        p.init(20, 100);
        CHECK(draw_card(p, card, Corner::TopRight, 0));
        const int left = 100 - CARD_MARGIN - size.cols;
        CHECK(at(p, 0, left + size.cols - 1) == U'╮');
        CHECK(at(p, 0, left + size.cols - 2) == U'─');
    }

    // ---- the cards' content
    {
        View view;
        view.set_cell_aspect(2.0);
        view.set_centre({37.69, -97.34});
        view.set_zoom_km(572);
        view.set_size(100, 22);
        Settings settings;
        settings.place_name = "Wichita, Kansas";
        const Card v = view_card(view, settings, "radar: scan 02:58 (08:58Z), 5 min old");
        CHECK(v.title == "Wichita, Kansas");
        CHECK(v.lines.size() == 3);
        CHECK(v.lines[0].text == "centre 37.690N 97.340W");
        CHECK(v.lines[1].text.find("572 km across, ") == 0);
        CHECK(v.lines[1].text.find(" km per column") != std::string::npos);
        CHECK(v.lines[2].text == "radar: scan 02:58 (08:58Z), 5 min old");
        view.set_centre({-33.9, 151.2});
        CHECK(view_card(view, settings, "").lines[0].text == "centre 33.900S 151.200E");

        const Card h = help_card();
        CHECK(h.title == "keys");
        CHECK(h.lines.size() == 9);
        CHECK(h.lines[0].key == "+ -" && h.lines[0].text == "zoom");
        CHECK(h.lines[8].key == "q" && h.lines[8].text == "quit");
        CHECK(h.lines[3].key == "f" && h.lines[3].text == "follow");
        for (const CardLine& line : h.lines) {                          // readable: not black, the keys brighter than the words
            CHECK(line.key_colour.r > 150 && line.key_colour.g > 150 && line.key_colour.b > 150);
            CHECK(line.text_colour.r > 100 && line.text_colour.g > 100 && line.text_colour.b > 100);
            CHECK(line.key_colour.r > line.text_colour.r);
        }
        for (const CardLine& line : v.lines) CHECK(line.text_colour.r > 100);

        const Card n = notes_card("first note\n\nsecond note\n");
        CHECK(n.title == "notes");
        CHECK(n.lines.size() == 2);                                   // the empty line is not a note
        CHECK(n.lines[0].text == "first note" && n.lines[1].text == "second note");
        CHECK(n.lines[0].text_colour.r == 255 && n.lines[0].text_colour.g == 190);    // amber
        CHECK(notes_card("").lines.empty());
    }

    // ---- drawing the stack
    {
        View view;
        view.set_centre({37.69, -97.34});
        view.set_zoom_km(572);
        view.set_size(100, 22);
        Settings settings;
        const std::string radar = "radar: scan 02:58 (08:58Z), 5 min old";

        vtscreen::Picture p;
        p.init(24, 100);                                              // tall enough for all three
        draw_cards(p, view, settings, "a warning", radar, true, true, false);
        // find the three titles, one under another (the view, the notes, the keys), and the frames between them
        auto find_title = [&](const std::string& word) {
            for (int i = 0; i < p.rows; i++)
                if (has(row_text(p, i, 0, p.cols), word)) return i;
            return -1;
        };
        const int view_top = 0;
        const int notes_top = find_title("notes");
        const int help_top = find_title("keys");
        CHECK(has(row_text(p, view_top, 0, p.cols), "Wichita"));
        CHECK(notes_top == card_size(view_card(view, settings, radar), 100).rows);
        CHECK(help_top == notes_top + card_size(notes_card("a warning"), 100).rows);
        CHECK(has(row_text(p, 1, 0, 100), "centre"));
        CHECK(find_title("radar: scan") == 3);
        // nothing is drawn in the last rows above the status bar, where the rain card goes
        for (int i = p.bar_row() - CARDS_FREE_ROWS_AT_BOTTOM; i < p.bar_row(); i++)
            CHECK(row_text(p, i, 0, 100).find(U'│') == std::u32string::npos);
        CHECK(!picture_is_blank(p));

        // the smallest screen, 100 x 23: the view card and the keys card fit (the usual case, no warnings); with a warning
        // the notes card is kept and the keys card is the one left out
        {
            vtscreen::Picture usual;
            usual.init(22, 100);
            draw_cards(usual, view, settings, "", radar, true, true, false);
            bool keys_there = false;
            for (int i = 0; i < usual.rows; i++) if (has(row_text(usual, i, 0, 100), "keys")) keys_there = true;
            CHECK(keys_there);
            vtscreen::Picture warned;
            warned.init(22, 100);
            draw_cards(warned, view, settings, "a warning", radar, true, true, false);
            bool keys_here = false, notes_here = false;
            for (int i = 0; i < warned.rows; i++) {
                if (has(row_text(warned, i, 0, 100), "keys")) keys_here = true;
                if (has(row_text(warned, i, 0, 100), "notes")) notes_here = true;
            }
            CHECK(notes_here && !keys_here);
        }

        // without the keys card and the notes card only the view card is there
        vtscreen::Picture q;
        q.init(22, 100);
        draw_cards(q, view, settings, "a warning", radar, false, false, false);
        bool keys = false, notes = false;
        for (int i = 0; i < q.rows; i++) {
            if (has(row_text(q, i, 0, 100), "keys")) keys = true;
            if (has(row_text(q, i, 0, 100), "notes")) notes = true;
        }
        CHECK(!keys && !notes);
        CHECK(has(row_text(q, 0, 0, 100), "Wichita"));

        // no notes, no notes card, even when asked
        vtscreen::Picture r;
        r.init(22, 100);
        draw_cards(r, view, settings, "", radar, false, true, false);
        for (int i = 0; i < r.rows; i++) CHECK(!has(row_text(r, i, 0, 100), "notes"));

        // a short screen: what does not fit under the others is left out, and what does is still drawn
        vtscreen::Picture s;
        s.init(14, 100);                                              // 11 usable rows: the view card (5) and the keys card (10) cannot both fit
        draw_cards(s, view, settings, "a warning", radar, true, true, false);
        bool short_keys = false, short_notes = false, short_view = false;
        for (int i = 0; i < s.rows; i++) {
            if (has(row_text(s, i, 0, 100), "keys")) short_keys = true;
            if (has(row_text(s, i, 0, 100), "notes")) short_notes = true;
            if (has(row_text(s, i, 0, 100), "Wichita")) short_view = true;
        }
        CHECK(short_view);
        CHECK(!short_keys);
        CHECK(short_notes);                                           // the warning comes before the keys, so it is the one kept

        // the stack stops short of the last rows above the status bar, where the rain card goes: the
        // view card (5 rows), a notes card with one note (3) and the keys card (11) need 19 rows
        auto keys_shown_with = [&](int picture_rows) {
            vtscreen::Picture t;
            t.init(picture_rows, 100);
            draw_cards(t, view, settings, "a warning", radar, true, true, false);
            for (int i = 0; i < t.rows; i++)
                if (has(row_text(t, i, 0, 100), "keys")) return true;
            return false;
        };
        CHECK(card_size(view_card(view, settings, radar), 100).rows == 5);
        CHECK(card_size(help_card(), 100).rows == 11);
        CHECK(card_size(notes_card("a warning"), 100).rows == 3);
        CHECK(!keys_shown_with(22));                                  // 22 rows, 4 kept free: 18 to use
        CHECK(keys_shown_with(23));                                   // 19 to use: just fits
    }

    // ---- the rain card: the key to the rain
    {
        const Card legend = legend_card();
        CHECK(legend.title == "rain");
        CHECK((int)legend.strip.size() == LEGEND_WIDTH);
        CHECK(card_size(legend, 100).cols == LEGEND_WIDTH + 4);
        CHECK(card_size(legend, 100).rows == 4);                      // the frame, the words and the strip

        View view;
        view.set_centre({37.69, -97.34});
        view.set_zoom_km(572);
        view.set_size(100, 29);
        Settings settings;
        vtscreen::Picture p;
        p.init(29, 100);
        draw_cards(p, view, settings, "", "", false, false, true);
        const int bottom = p.bar_row() - 1;                           // the last row above the status bar
        const int left = 100 - CARD_MARGIN - (LEGEND_WIDTH + 4);
        CHECK(at(p, bottom, left) == U'╰' && at(p, bottom, 99 - CARD_MARGIN) == U'╯');
        CHECK(at(p, bottom - 3, left) == U'╭');
        CHECK(has(row_text(p, bottom - 3, left, LEGEND_WIDTH + 4), "rain"));
        const std::u32string words = row_text(p, bottom - 2, left + 2, LEGEND_WIDTH);
        CHECK(has(words, "light rain") && has(words, "heavy"));
        CHECK(words.find(U"light rain") < words.find(U"heavy"));
        CHECK(words.size() == (size_t)LEGEND_WIDTH);
        CHECK(words.find(U"light rain") == 0);                         // one at each end of the strip
        CHECK(words.rfind(U"heavy") == words.size() - 5);
        // the strip: cells of the rain's own kind, brighter and changing colour along it, holding still
        const int strip = bottom - 1;
        double before = 0;
        for (int i = 0; i < LEGEND_WIDTH; i++) {
            const vtscreen::PicCell& c = p.at(strip, left + 2 + i);
            CHECK(!c.text && c.lum >= before && c.lum > 0.3 && c.shimmer == 0);
            CHECK(c.alt_lum > 0);                                     // with the card's background behind the letters
            before = c.lum;
        }
        CHECK_NEAR(p.at(strip, left + 2).lum, rain_look(5).brightness, 1e-12);
        CHECK_NEAR(p.at(strip, left + 2 + LEGEND_WIDTH - 1).lum, rain_look(60).brightness, 1e-12);
        CHECK(p.at(strip, left + 2 + 14).g != p.at(strip, left + 2).g || p.at(strip, left + 2 + 14).r != p.at(strip, left + 2).r);
        CHECK(at(p, strip, left) == U'│' && at(p, strip, left + LEGEND_WIDTH + 3) == U'│');
        for (int c = 0; c < p.cols; c++) CHECK(!p.at(p.bar_row(), c).text && p.at(p.bar_row(), c).lum == 0);   // the status bar is left alone

        // not asked for: not there
        vtscreen::Picture none;
        none.init(29, 100);
        draw_cards(none, view, settings, "", "", false, false, false);
        for (int i = 0; i < none.rows; i++) CHECK(!has(row_text(none, i, 0, 100), "rain"));
        // too small a screen: nothing
        for (const std::pair<int, int>& size : {std::pair<int, int>{69, 30}, {100, 15}, {40, 20}}) {
            vtscreen::Picture small;
            small.init(size.second, size.first);
            draw_cards(small, view, settings, "", "", false, false, true);
            for (int i = 0; i < small.rows; i++) CHECK(!has(row_text(small, i, 0, size.first), "rain"));
        }
        // just big enough
        vtscreen::Picture least;
        least.init(LEGEND_MIN_ROWS, LEGEND_MIN_COLS);
        draw_cards(least, view, settings, "", "", false, false, true);
        bool seen = false;
        for (int i = 0; i < least.rows; i++) if (has(row_text(least, i, 0, LEGEND_MIN_COLS), "rain")) seen = true;
        CHECK(seen);
    }

    // ---- the driving card: the GPS in miles per hour and feet
    {
        GpsFix fix;
        fix.fix = true;
        fix.lat = 37.69012;
        fix.lon = -97.34098;
        fix.speed_kmph = 100.0;                    // 62.1 mph
        fix.heading_deg = 87.0;
        fix.heading_valid = true;
        fix.altitude_m = 412.0;                    // 1351.7 ft
        fix.satellites = 9;
        Car car;
        car.update(GpsState::Fix, fix);
        CHECK(car.arrow());

        // a fix, moving: the position, the speed with the heading, the height and satellites
        Card c = driving_card(GpsState::Fix, fix, "", 0.2, car);
        CHECK(c.title == "driving");
        CHECK(driving_card(GpsState::Fix, fix, "", 0.2, car, true).title == "driving, following");        // while the map follows the car
        CHECK(driving_card(GpsState::Stale, fix, "", 5, car, true).title == "driving, following");
        CHECK(driving_card(GpsState::Fix, fix, "", 0.2, car, false).title == "driving");
        CHECK(c.lines.size() == 3);
        CHECK(c.lines[0].text == "37.690N 97.341W");
        CHECK(c.lines[1].text == "62 mph   heading 87° E");
        CHECK(c.lines[2].text == "1352 ft   9 satellites");
        CHECK(c.border_colour.r == Card().border_colour.r && c.border_colour.g == Card().border_colour.g);   // the usual frame: not a warning
        CHECK(c.lines[1].text_colour.r > c.lines[0].text_colour.r);                                         // the speed stands out

        // slow: the speed alone, no heading
        GpsFix slow = fix;
        slow.speed_kmph = 2.0;
        Car stopped;
        stopped.update(GpsState::Fix, slow);
        Card s = driving_card(GpsState::Fix, slow, "", 0.2, stopped);
        CHECK(s.lines[1].text == "1 mph");
        CHECK(s.lines[1].text.find("heading") == std::string::npos);
        // moving, but the GPS does not trust the heading: no heading either
        GpsFix unsure = fix;
        unsure.heading_valid = false;
        Car unsure_car;
        unsure_car.update(GpsState::Fix, unsure);
        CHECK(driving_card(GpsState::Fix, unsure, "", 0.2, unsure_car).lines[1].text == "62 mph");
        // the heading names, other directions
        GpsFix south_west = fix;
        south_west.heading_deg = 225.0;
        Car sw_car;
        sw_car.update(GpsState::Fix, south_west);
        CHECK(driving_card(GpsState::Fix, south_west, "", 0.2, sw_car).lines[1].text == "62 mph   heading 225° SW");
        // southern and western places, a negative height
        GpsFix south = fix;
        south.lat = -33.86;
        south.lon = 151.21;
        south.altitude_m = -2.0;
        CHECK(driving_card(GpsState::Fix, south, "", 0.2, car).lines[0].text == "33.860S 151.210E");
        CHECK(driving_card(GpsState::Fix, south, "", 0.2, car).lines[2].text == "-7 ft   9 satellites");

        // no fix yet: amber, with the satellites
        GpsFix none = fix;
        none.fix = false;
        none.satellites = 3;
        Card n = driving_card(GpsState::NoFix, none, "", 0.2, car);
        CHECK(n.lines.size() == 2 && n.lines[0].text == "no fix yet" && n.lines[1].text == "3 satellites");
        CHECK(n.border_colour.r == 200 && n.border_colour.g == 150 && n.title_colour.g == 190);              // amber, like the notes card
        CHECK(n.lines[0].text_colour.r == 255 && n.lines[0].text_colour.g == 190);

        // lost: how long, and where it last was
        Card lost = driving_card(GpsState::Stale, fix, "", 13.7, car);
        CHECK(lost.lines.size() == 2 && lost.lines[0].text == "lost, nothing for 13 s");
        CHECK(lost.lines[1].text == "last known 37.690N 97.341W");
        CHECK(lost.border_colour.r == 200);
        CHECK(driving_card(GpsState::Stale, fix, "", -3.0, car).lines[0].text == "lost, nothing for 0 s");      // a clock set back

        // unreadable: and why
        Card bad = driving_card(GpsState::Bad, fix, "not valid JSON", 0.2, car);
        CHECK(bad.lines.size() == 2 && bad.lines[0].text == "unreadable" && bad.lines[1].text == "not valid JSON");
        CHECK(bad.border_colour.r == 200);

        // no GPS to talk about: no lines, so nothing is drawn
        CHECK(driving_card(GpsState::Off, fix, "", 0, car).lines.empty());
        CHECK(driving_card(GpsState::NoFile, fix, "", 0, car).lines.empty());

        // drawn in the bottom left corner, above the status bar, and not when there are no lines or not asked for
        View view;
        view.set_centre({37.69, -97.34});
        view.set_zoom_km(572);
        view.set_size(100, 22);
        Settings settings;
        vtscreen::Picture p;
        p.init(22, 100);
        draw_cards(p, view, settings, "", "", false, false, false, &c);
        const CardSize size = card_size(c, 100);
        const int top = p.bar_row() - size.rows;
        CHECK(at(p, top, CARD_MARGIN) == U'╭');
        CHECK(at(p, p.bar_row() - 1, CARD_MARGIN) == U'╰');
        CHECK(has(row_text(p, top, 0, 100), "driving"));
        CHECK(has(row_text(p, top + 2, 0, 100), "62 mph"));
        vtscreen::Picture q;
        q.init(22, 100);
        draw_cards(q, view, settings, "", "", false, false, false, nullptr);
        for (int i = 0; i < q.rows; i++) CHECK(!has(row_text(q, i, 0, 100), "driving"));
        const Card empty = driving_card(GpsState::NoFile, fix, "", 0, car);
        draw_cards(q, view, settings, "", "", false, false, false, &empty);
        for (int i = 0; i < q.rows; i++) CHECK(!has(row_text(q, i, 0, 100), "driving"));
        // both bottom cards at once: the driving card on the left, the rain card on the right, no overlap
        vtscreen::Picture both;
        both.init(22, 100);
        draw_cards(both, view, settings, "", "", false, false, true, &c);
        CHECK(has(row_text(both, both.bar_row() - 4, 0, 100), "driving") || has(row_text(both, both.bar_row() - 5, 0, 100), "driving"));
        bool rain = false;
        for (int i = 0; i < both.rows; i++) if (has(row_text(both, i, 0, 100), "rain")) rain = true;
        CHECK(rain);
        CHECK(CARD_MARGIN + size.cols < 100 - CARD_MARGIN - (LEGEND_WIDTH + 4));     // they do not touch at 100 columns
    }

    // ---- when the keys card and the notes card come and go
    {
        CardsState state;
        CHECK(state.help_visible(0, false));                          // shown at the start
        CHECK(state.help_visible(29.9, false));
        CHECK(!state.help_visible(30.0, false));                      // 30 seconds after the last key
        CHECK(!state.help_visible(500, false));
        state.key_pressed(100);
        CHECK(state.help_visible(100, false));                        // any key brings it back
        CHECK(state.help_visible(129.9, false));
        CHECK(!state.help_visible(130, false));
        state.key_pressed(120);                                       // every key press starts the 30 seconds again
        CHECK(state.help_visible(149.9, false));
        CHECK(!state.help_visible(150, false));
        CHECK(!state.help_visible(125, true));                        // never while the go-to prompt is open

        CardsState fresh;
        CHECK(fresh.notes_visible("a warning"));
        CHECK(!fresh.notes_visible(""));
        fresh.key_pressed(1);
        CHECK(!fresh.notes_visible("a warning"));                     // gone after the first key press
        CHECK(!fresh.notes_visible("a warning"));
    }

    if (failures == 0) std::printf("all card tests passed\n");
    return failures == 0 ? 0 : 1;
}
