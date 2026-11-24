// Tests for what the program keeps on disk: radar pictures (their coding, damaged files, the
// slots and how old a picture may be) and the remembered view.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>

#include "check.h"

#include "../src/store.h"

namespace {

namespace fs = std::filesystem;
using vtw::GeoBox;
using vtw::RadarFrame;

RadarFrame make_frame(int width, int height, uint32_t seed, double south = 40) {
    RadarFrame f;
    f.box = {south, -100, south + 2, -97};
    f.width = width; f.height = height;
    f.dbz.assign((size_t)width * height, 0);
    std::mt19937 rng(seed);
    for (size_t i = 0; i < f.dbz.size(); i++)
        if (rng() % 7 == 0) f.dbz[i] = (uint8_t)(1 + rng() % 75);      // some rain among the nothing
    return f;
}

std::string temp_folder(const char* name) {
    const fs::path path = fs::temp_directory_path() / (std::string("vtw_store_") + name + "_" + std::to_string(getpid()));
    fs::remove_all(path);
    return path.string();
}

void picture_tests() {
    // a picture goes to bytes and comes back the same, whatever it holds
    for (int width : {1, 7, 100, 301}) {
        const RadarFrame frame = make_frame(width, 37, (uint32_t)width);
        const std::string bytes = vtw::encode_picture(frame, 1766000123.5);
        vtw::StoredPicture back;
        std::string error;
        CHECK(vtw::decode_picture((const uint8_t*)bytes.data(), bytes.size(), back, error));
        CHECK(back.frame.width == width && back.frame.height == 37);
        CHECK(back.frame.dbz == frame.dbz);
        CHECK_NEAR(back.unix_time, 1766000123.5, 1e-9);
        CHECK(back.frame.box.south == 40 && back.frame.box.east == -97);
    }
    // mostly no rain codes small, and a run longer than a run can hold is split
    {
        RadarFrame empty;
        empty.box = {0, 0, 1, 1};
        empty.width = 400; empty.height = 400;                   // 160 000 pixels of nothing
        empty.dbz.assign(160000, 0);
        const std::string bytes = vtw::encode_picture(empty, 5);
        CHECK(bytes.size() < 200);
        vtw::StoredPicture back;
        std::string error;
        CHECK(vtw::decode_picture((const uint8_t*)bytes.data(), bytes.size(), back, error) && back.frame.dbz == empty.dbz);
    }
    // every shortened version is refused, and damaged ones never crash or give a wrong size
    const RadarFrame frame = make_frame(60, 40, 5);
    const std::string bytes = vtw::encode_picture(frame, 1000);
    for (size_t length = 0; length < bytes.size(); length++) {
        vtw::StoredPicture p;
        std::string error;
        CHECK(!vtw::decode_picture((const uint8_t*)bytes.data(), length, p, error) && p.frame.dbz.empty());
    }
    std::mt19937 rng(99);
    int refused = 0;
    for (int round = 0; round < 4000; round++) {
        std::string damaged = bytes;
        for (int k = 0; k < 1 + (int)(rng() % 3); k++) damaged[rng() % damaged.size()] = (char)(rng() % 256);
        vtw::StoredPicture p;
        std::string error;
        if (!vtw::decode_picture((const uint8_t*)damaged.data(), damaged.size(), p, error)) refused++;
        else CHECK(p.frame.dbz.size() == (size_t)p.frame.width * p.frame.height);
    }
    CHECK(refused > 1000);
    std::string wrong = bytes;
    wrong[0] = 'X';
    vtw::StoredPicture p;
    std::string error;
    CHECK(!vtw::decode_picture((const uint8_t*)wrong.data(), wrong.size(), p, error) && error.find("not a stored") != std::string::npos);
    std::string trailing = bytes + "x";
    CHECK(!vtw::decode_picture((const uint8_t*)trailing.data(), trailing.size(), p, error));
    // a size that cannot be true is refused before any memory is used
    RadarFrame big = make_frame(10, 10, 1);
    std::string huge = vtw::encode_picture(big, 1);
    for (int i = 0; i < 4; i++) huge[8 + 8 + 32 + i] = (char)0xFF;       // the width: four billion
    CHECK(!vtw::decode_picture((const uint8_t*)huge.data(), huge.size(), p, error) && error.find("size") != std::string::npos);
}

void slot_tests() {
    const std::string folder = temp_folder("slots");
    vtw::RadarStore store(folder);
    std::string error;
    CHECK(store.load(2000).empty());                                  // nothing there yet, and no folder
    // six saves into four slots: the four newest are kept, newest first
    for (int i = 0; i < 6; i++) CHECK(store.save(make_frame(20, 10, (uint32_t)i, 30 + i), 1000 + i * 100, error));
    std::vector<vtw::StoredPicture> got = store.load(2000);
    CHECK(got.size() == 4);
    for (size_t i = 0; i < got.size(); i++) {
        CHECK_NEAR(got[i].unix_time, 1000 + (5 - (double)i) * 100, 1e-9);
        CHECK(got[i].frame.box.south == 30 + (5 - (double)i));
    }
    // too old are skipped, and so are pictures from the future
    CHECK(store.load(1200 + vtw::STORED_MAX_AGE_SECONDS + 50).size() == 3);        // the oldest of the four (1200) is past the age limit
    CHECK(store.load(100).empty());
    // a damaged file is skipped, the others still load
    {
        std::ofstream(folder + "/radar_0.bin", std::ios::binary | std::ios::trunc) << "not a picture";
        CHECK(store.load(2000).size() == 3);
        std::ofstream(folder + "/radar_1.bin", std::ios::binary | std::ios::trunc);
        CHECK(store.load(2000).size() == 2);
    }
    // the next save goes into a damaged or empty slot first
    CHECK(store.save(make_frame(5, 5, 77, 50), 3000, error));
    CHECK(store.save(make_frame(5, 5, 78, 51), 3100, error));
    got = store.load(3200);
    CHECK(got.size() == 4);
    CHECK_NEAR(got[0].unix_time, 3100, 1e-9);
    // a folder that cannot be made is reported, not a crash
    vtw::RadarStore nowhere("/proc/vtweather_cannot_exist_here");
    CHECK(!nowhere.save(make_frame(5, 5, 1), 1, error) && !error.empty());
    fs::remove_all(folder);
}

void view_tests() {
    const std::string folder = temp_folder("view");
    vtw::ViewState state;
    CHECK(!vtw::load_view_state(folder, state));                      // nothing saved
    vtw::ViewState saved;
    saved.centre = {47.25, -122.5};
    saved.zoom_km = 333.5;
    saved.graticule = true;
    saved.names = false;
    std::string error;
    CHECK(vtw::save_view_state(folder, saved, error));
    CHECK(vtw::load_view_state(folder, state));
    CHECK_NEAR(state.centre.lat, 47.25, 1e-12);
    CHECK_NEAR(state.centre.lon, -122.5, 1e-12);
    CHECK_NEAR(state.zoom_km, 333.5, 1e-12);
    CHECK(state.graticule && !state.names);
    // damaged, wrong, or impossible files are ignored
    auto write = [&](const std::string& text) { std::ofstream(folder + "/view.json", std::ios::binary | std::ios::trunc) << text; };
    for (const char* text : {"{ not json", "[]", "{\"lat\":1}", "{\"lat\":\"x\",\"lon\":1,\"zoom_km\":2}", "{\"lat\":120,\"lon\":1,\"zoom_km\":2}",
                             "{\"lat\":10,\"lon\":1000,\"zoom_km\":2}", ""}) {
        write(text);
        vtw::ViewState untouched = saved;
        CHECK(!vtw::load_view_state(folder, untouched));
        CHECK(untouched.zoom_km == 333.5);                               // not changed by a refusal
    }
    write("{\"lat\":10,\"lon\":20,\"zoom_km\":30}");                    // the layer flags are optional
    CHECK(vtw::load_view_state(folder, state) && state.names && !state.graticule);
    fs::remove_all(folder);
}

void folder_tests() {
    const char* old_xdg = std::getenv("XDG_CACHE_HOME");
    const std::string saved_xdg = old_xdg ? old_xdg : "";
    setenv("XDG_CACHE_HOME", "/tmp/some_cache", 1);
    CHECK(vtw::default_cache_dir() == "/tmp/some_cache/vtweather");
    unsetenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    if (home) CHECK(vtw::default_cache_dir() == std::string(home) + "/.cache/vtweather");
    if (old_xdg) setenv("XDG_CACHE_HOME", saved_xdg.c_str(), 1);
}

}  // namespace

int main() {
    picture_tests();
    slot_tests();
    view_tests();
    folder_tests();
    if (failures == 0) std::printf("all store tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
