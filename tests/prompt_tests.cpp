// Tests for reading keys (arrows, Escape, Enter, Backspace) and for the go-to prompt (matching city
// names, choosing one, jumping there, and drawing the prompt).
#include <algorithm>
#include <cctype>
#include <random>
#include <string>
#include <vector>

#include "check.h"

#include "../src/prompt.h"

#ifndef VTW_DATA_DIR
#error "VTW_DATA_DIR must name the data folder"
#endif

namespace {

using vtw::Key;
using vtw::KeyKind;
using vtw::KeyReader;

// feed text as the terminal would send it and collect the keys
std::vector<Key> read(const std::string& text, bool flush_at_end = true) {
    KeyReader r;
    for (char c : text) r.feed(c);
    if (flush_at_end) r.flush();
    std::vector<Key> keys;
    Key k;
    while (r.next(k)) keys.push_back(k);
    return keys;
}

std::vector<KeyKind> kinds(const std::vector<Key>& keys) {
    std::vector<KeyKind> out;
    for (const Key& k : keys) out.push_back(k.kind);
    return out;
}

void key_tests() {
    using K = KeyKind;
    // ordinary characters, Enter (either way it is sent) and Backspace (either code)
    CHECK(kinds(read("ab")) == (std::vector<K>{K::Char, K::Char}));
    CHECK(read("ab")[1].ch == 'b');
    CHECK(kinds(read("\r")) == std::vector<K>{K::Enter} && kinds(read("\n")) == std::vector<K>{K::Enter});
    CHECK(kinds(read("\x7f")) == std::vector<K>{K::Backspace} && kinds(read("\x08")) == std::vector<K>{K::Backspace});
    // arrows, in both of the ways terminals send them
    CHECK(kinds(read("\x1b[A\x1b[B\x1b[C\x1b[D")) == (std::vector<K>{K::Up, K::Down, K::Right, K::Left}));
    CHECK(kinds(read("\x1bOA\x1bOB\x1bOC\x1bOD")) == (std::vector<K>{K::Up, K::Down, K::Right, K::Left}));
    // an arrow with modifiers (Ctrl or Shift) is still an arrow; other sequences are ignored whole
    CHECK(kinds(read("\x1b[1;5C")) == std::vector<K>{K::Right});
    CHECK(read("\x1b[3~").empty());                                  // Delete
    CHECK(read("\x1b[15~x").size() == 1 && read("\x1b[15~x")[0].ch == 'x');
    CHECK(read("\x1b[H").empty());                                   // Home
    // a lone Escape is an Escape once the frame's characters are done, not before
    CHECK(read("\x1b", false).empty());
    CHECK(kinds(read("\x1b")) == std::vector<K>{K::Escape});
    // Escape followed by an ordinary key: both
    CHECK(kinds(read("\x1bx")) == (std::vector<K>{K::Escape, K::Char}));
    CHECK(kinds(read("a\x1b\r")) == (std::vector<K>{K::Char, K::Escape, K::Enter}));
    // a sequence that arrives in two pieces, with a flush between, is still one arrow if the flush
    // was not in the middle of it (the reader only turns a bare Escape into a key)
    {
        KeyReader r;
        r.feed('\x1b'); r.feed('[');
        r.flush();                                                   // Escape and [ are already a sequence: nothing yet
        Key k;
        CHECK(!r.next(k));
        r.feed('A');
        CHECK(r.next(k) && k.kind == K::Up);
    }
    // whatever the terminal sends, nothing crashes and every key is a real kind
    std::mt19937 rng(5);
    for (int round = 0; round < 3000; round++) {
        std::string noise;
        for (int i = 0; i < 40; i++) noise += (char)(rng() % 4 == 0 ? 27 : rng() % 256);
        for (const Key& k : read(noise)) CHECK(k.kind != K::None);
    }
}

vtw::View make_view(double lat, double lon, double km) {
    vtw::View v;
    v.set_cell_aspect(2.0);
    v.set_centre({lat, lon});
    v.set_zoom_km(km);
    v.set_size(100, 40);
    return v;
}

Key ch(char c) { Key k; k.kind = KeyKind::Char; k.ch = c; return k; }
Key kind(KeyKind kd) { Key k; k.kind = kd; return k; }

void type(vtw::PromptState& p, const std::string& text, const vtw::BaseMap& map, vtw::View& view) {
    for (char c : text) vtw::prompt_key(p, ch(c), map, view);
}

void match_tests(const vtw::BaseMap& map) {
    auto names = [&](const std::string& typed) {
        std::vector<std::string> out;
        for (const vtw::MapPlace* p : vtw::match_places(map, typed)) out.push_back(p->name);
        return out;
    };
    CHECK(names("wichita").size() >= 1 && names("wichita")[0] == "Wichita");
    CHECK(names("WICHITA")[0] == "Wichita");                         // case does not matter
    CHECK(names("  wichita  ")[0] == "Wichita");                     // nor spaces round it
    CHECK(names("Wich")[0] == "Wichita");                            // a start is enough
    CHECK(names("new york")[0] == "New York");
    CHECK(names("").empty() && names("   ").empty());
    CHECK(names("zzzzqqq").empty());
    // starting with it comes before merely containing it, and the biggest comes first
    {
        const std::vector<std::string> n = names("sea");
        CHECK(!n.empty() && n[0] == "Seattle");
        const std::vector<const vtw::MapPlace*> m = vtw::match_places(map, "ton");
        bool seen_containing = false;
        uint32_t last_start = 0xFFFFFFFF, last_contain = 0xFFFFFFFF;
        for (const vtw::MapPlace* p : m) {
            const bool starts = p->name.size() >= 3 && (p->name[0] | 32) == 't' && (p->name[1] | 32) == 'o' && (p->name[2] | 32) == 'n';
            if (!starts) seen_containing = true;
            CHECK(!(starts && seen_containing));                     // no start-match after a contains-match
            if (starts) { CHECK(p->population <= last_start); last_start = p->population; }
            else { CHECK(p->population <= last_contain); last_contain = p->population; }
        }
    }
    // whenever some name starts with what was typed, the first match is one of those
    for (const char* typed : {"ton", "san", "mon", "port", "lake", "el "}) {
        bool any_starts = false;
        const std::string t = typed;
        for (const vtw::MapPlace& p : map.places) {
            std::string low = p.name;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if (low.compare(0, t.size(), t) == 0) any_starts = true;
        }
        const std::vector<const vtw::MapPlace*> m = vtw::match_places(map, t);
        CHECK(!any_starts || (!m.empty() && m[0]->name.size() >= t.size() && [&] {
            std::string low = m[0]->name;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            return low.compare(0, t.size(), t) == 0;
        }()));
    }
    // several places of one name are all there, with the biggest first
    {
        const std::vector<const vtw::MapPlace*> m = vtw::match_places(map, "portland");
        CHECK(m.size() >= 2);
        if (m.size() >= 2) CHECK(m[0]->population >= m[1]->population);
    }
    CHECK(vtw::match_places(map, "a", 3).size() == 3);
    CHECK(vtw::match_places(map, "a").size() == vtw::MAX_SUGGESTIONS);
}

void prompt_tests(const vtw::BaseMap& map) {
    // typing, Backspace, and jumping to the best match
    {
        vtw::View view = make_view(40, -100, 80);
        vtw::PromptState p;
        p.active = true;
        type(p, "Wichit", map, view);
        CHECK(p.typed == "Wichit");
        vtw::prompt_key(p, kind(KeyKind::Backspace), map, view);
        CHECK(p.typed == "Wichi");
        type(p, "tax", map, view);
        CHECK(p.typed == "Wichitax");                                 // no match now
        CHECK(vtw::prompt_key(p, kind(KeyKind::Enter), map, view) == vtw::PromptResult::Stay);     // nothing to go to
        CHECK(p.active && view.centre().lat == 40);
        vtw::prompt_key(p, kind(KeyKind::Backspace), map, view);
        CHECK(vtw::prompt_key(p, kind(KeyKind::Enter), map, view) == vtw::PromptResult::Jumped);
        CHECK(!p.active && p.typed.empty());
        CHECK_NEAR(view.centre().lat, 37.69, 0.1);
        CHECK_NEAR(view.centre().lon, -97.34, 0.1);
        CHECK_NEAR(view.zoom_km(), 80, 1e-9);                         // a close view keeps its zoom
    }
    // from far out a jump zooms in
    {
        vtw::View view = make_view(40, -100, 3000);
        vtw::PromptState p;
        p.active = true;
        type(p, "denver", map, view);
        CHECK(vtw::prompt_key(p, kind(KeyKind::Enter), map, view) == vtw::PromptResult::Jumped);
        CHECK_NEAR(view.centre().lat, 39.7, 0.2);
        CHECK_NEAR(view.zoom_km(), vtw::ZOOM_ON_JUMP_KM, 1e-9);
    }
    // Escape gives up and changes nothing
    {
        vtw::View view = make_view(40, -100, 80);
        vtw::PromptState p;
        p.active = true;
        type(p, "chicago", map, view);
        CHECK(vtw::prompt_key(p, kind(KeyKind::Escape), map, view) == vtw::PromptResult::Cancelled);
        CHECK(!p.active && p.typed.empty() && view.centre().lat == 40 && view.centre().lon == -100);
    }
    // Up and Down choose among several, wrapping round; Enter goes to the chosen one
    {
        vtw::View view = make_view(40, -100, 80);
        vtw::PromptState p;
        p.active = true;
        type(p, "portland", map, view);
        const auto m = vtw::match_places(map, "portland");
        CHECK(m.size() >= 2);
        vtw::prompt_key(p, kind(KeyKind::Down), map, view);
        CHECK(p.selected == 1);
        vtw::prompt_key(p, kind(KeyKind::Up), map, view);
        vtw::prompt_key(p, kind(KeyKind::Up), map, view);
        CHECK(p.selected == (int)m.size() - 1);                       // wrapped round to the last
        vtw::prompt_key(p, kind(KeyKind::Down), map, view);
        vtw::prompt_key(p, kind(KeyKind::Down), map, view);
        CHECK(p.selected == 1);
        vtw::prompt_key(p, kind(KeyKind::Enter), map, view);
        CHECK_NEAR(view.centre().lat, m[1]->lat / vtw::MAP_UNITS_PER_DEGREE, 0.01);
        CHECK_NEAR(view.centre().lon, m[1]->lon / vtw::MAP_UNITS_PER_DEGREE, 0.01);
        // typing more starts again from the first
        vtw::PromptState q;
        q.active = true;
        type(q, "portlan", map, view);
        vtw::prompt_key(q, kind(KeyKind::Down), map, view);
        vtw::prompt_key(q, ch('d'), map, view);
        CHECK(q.selected == 0);
    }
    // only printable characters are typed, up to a limit
    {
        vtw::View view = make_view(40, -100, 80);
        vtw::PromptState p;
        p.active = true;
        vtw::prompt_key(p, ch('\x01'), map, view);
        vtw::prompt_key(p, kind(KeyKind::Left), map, view);
        CHECK(p.typed.empty());
        for (int i = 0; i < 100; i++) vtw::prompt_key(p, ch('x'), map, view);
        CHECK(p.typed.size() == vtw::MAX_TYPED);
    }
}

void draw_tests(const vtw::BaseMap& map) {
    vtw::View view = make_view(40, -100, 80);
    for (const std::pair<int, int>& size : {std::pair<int, int>{100, 40}, {40, 20}, {60, 12}}) {
        vtscreen::Picture picture;
        picture.init(size.second, size.first);
        vtw::PromptState closed;
        vtw::draw_prompt(closed, map, picture);                       // closed: nothing drawn
        for (const vtscreen::PicCell& c : picture.cell) CHECK(!c.text);

        vtw::PromptState p;
        p.active = true;
        type(p, "wich", map, view);
        vtw::draw_prompt(p, map, picture);
        // the typing line is the last row above the bar and shows what was typed; the match is above it
        auto row_text = [&](int row) {
            std::string s;
            for (int c = 0; c < picture.cols; c++) s += picture.at(row, c).text ? (char)picture.at(row, c).ch : ' ';
            return s;
        };
        const int bottom = picture.bar_row() - 1;
        CHECK(row_text(bottom).find("go to: wich_") != std::string::npos);
        CHECK(row_text(bottom - 1).find("Wichita") != std::string::npos);
        CHECK(picture.at(bottom - 1, 3).inv);                         // the chosen one is shown inverse
        for (int c = 0; c < picture.cols; c++) CHECK(!picture.at(picture.bar_row(), c).text);   // the bar row is left alone
    }
    // no match: a line says so
    vtscreen::Picture picture;
    picture.init(30, 80);
    vtw::PromptState p;
    p.active = true;
    type(p, "zzzqq", map, view);
    vtw::draw_prompt(p, map, picture);
    std::string line;
    for (int c = 0; c < picture.cols; c++) line += picture.at(picture.bar_row() - 2, c).text ? (char)picture.at(picture.bar_row() - 2, c).ch : ' ';
    CHECK(line.find("no city like that") != std::string::npos);
}

}  // namespace

int main() {
    key_tests();
    vtw::BaseMap map;
    std::string error;
    CHECK(vtw::load_basemap(std::string(VTW_DATA_DIR) + "/basemap.bin", map, error));
    match_tests(map);
    prompt_tests(map);
    draw_tests(map);
    if (failures == 0) std::printf("all prompt tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
