// Tests for the part of the vtscreen library vtweather leans on: how a cell's shimmer picks its letter.
// A shimmer of 0 shows the first letter of the brightness group, the same in every such cell; any shimmer
// above 0, however small, picks by position and moves at that speed (as it always did).
#include <set>
#include <string>

#include "check.h"

#include "vtscreen/vtscreen.h"

namespace {

using vtscreen::LEVELS;

// the brightness that falls in step s of the 16 (the middle of the step)
double lum_of_step(int s) { return (s + 0.5) / (2.0 * (double)LEVELS.size()); }

void still_tests() {
    const int fps = 30;
    for (bool hash : {false, true})
        for (int s = 0; s < 2 * (int)LEVELS.size(); s++) {
            const bool inverse = s >= (int)LEVELS.size();
            const std::string& group = LEVELS[inverse ? 2 * LEVELS.size() - 1 - s : s];
            for (double shimmer : {0.0, -1.0, -0.001}) {
                std::set<char> seen;
                for (int i = 0; i < 24; i++)
                    for (int j = 0; j < 24; j++)
                        for (double t : {0.0, 0.7, 13.3, 500.0}) {
                            bool inv = false;
                            seen.insert(vtscreen::pick_char(lum_of_step(s), i, j, t, fps, inv, hash, shimmer));
                            CHECK(inv == inverse);
                        }
                CHECK(seen.size() == 1 && *seen.begin() == group[0]);           // the group's first letter, always
            }
        }
    // the first letters, for reference: a space, then . : = i o 0 #
    CHECK(LEVELS.size() == 8);
    const char first[8] = {' ', '.', ':', '=', 'i', 'o', '0', '#'};
    for (size_t g = 0; g < 8; g++) CHECK(LEVELS[g][0] == first[g]);
}

void moving_tests() {
    const int fps = 30;
    // any shimmer above 0, however small, picks by position: a flat patch shows several different letters
    for (int s = 1; s < (int)LEVELS.size(); s++) {                              // every group with more than one letter
        if (LEVELS[s].size() < 2) continue;
        for (double shimmer : {1e-9, 0.001, 0.05, 1.0}) {
            std::set<char> seen;
            for (int i = 0; i < 24; i++)
                for (int j = 0; j < 24; j++) {
                    bool inv;
                    seen.insert(vtscreen::pick_char(lum_of_step(s), i, j, 0.0, fps, inv, false, shimmer));
                }
            CHECK(seen.size() == LEVELS[s].size());                              // all the letters of the group appear
        }
    }
    // and at shimmer 1 it is exactly what it always was: position, plus a step every SHIMMER_FRAMES frames
    for (int s : {1, 2, 3, 4, 5, 6, 7})
        for (int i = 0; i < 12; i++)
            for (int j = 0; j < 12; j++)
                for (double t : {0.0, 0.5, 3.25, 40.0}) {
                    bool inv;
                    const std::string& g = LEVELS[s];
                    const long ticks = (long)(t * fps / vtscreen::SHIMMER_FRAMES * 1.0);
                    const char expected = g[((long)i * 7 + (long)j * 13 + ticks) % (long)g.size()];
                    CHECK(vtscreen::pick_char(lum_of_step(s), i, j, t, fps, inv, false, 1.0) == expected);
                }
    // a slow one moves slowly: the same a while later, different much later
    {
        bool inv;
        const double lum = lum_of_step(4);
        int same_at_5s = 0, same_at_300s = 0;
        for (int i = 0; i < 20; i++)
            for (int j = 0; j < 20; j++) {
                const char a = vtscreen::pick_char(lum, i, j, 0.0, fps, inv, false, 0.001);
                same_at_5s += vtscreen::pick_char(lum, i, j, 5.0, fps, inv, false, 0.001) == a;
                same_at_300s += vtscreen::pick_char(lum, i, j, 300.0, fps, inv, false, 0.001) == a;
            }
        CHECK(same_at_5s == 400);
        CHECK(same_at_300s < 400);
    }
}

void render_tests() {
    // a flat area with shimmer 0 renders as one letter, and with a little shimmer as a mixture
    vtscreen::Picture flat;
    flat.init(10, 30);
    for (vtscreen::PicCell& c : flat.cell) { c.lum = 0.3; c.shimmer = 0; c.r = c.g = c.b = 200; }
    auto letters = [&](const vtscreen::Picture& p) {
        std::set<char> seen;
        const std::string text = vtscreen::render_plain(p, 10, 30, 2.0, 30, false, false, false);
        for (char c : text) if (c != '\n') seen.insert(c);
        return seen;
    };
    CHECK(letters(flat).size() == 1);
    for (vtscreen::PicCell& c : flat.cell) c.shimmer = 0.001;
    CHECK(letters(flat).size() > 1);
}

}  // namespace

int main() {
    still_tests();
    moving_tests();
    render_tests();
    if (failures == 0) std::printf("all library tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
