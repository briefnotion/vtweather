// Store: what the program keeps on disk between runs, in a folder of its own (by default
// ~/.cache/vtweather, or $XDG_CACHE_HOME/vtweather; the `cache_dir` setting names another):
//   - the last few radar pictures, so a restart shows rain at once, and with no network the last
//     picture is still there (marked with its age);
//   - where you were looking (the centre, the zoom and which layers were on), so you come back to it.
// Everything here stays on your machine, outside the repository. A missing or damaged file is
// never an error: the program just starts without it.
#pragma once

#include <string>
#include <vector>

#include "radar_frame.h"
#include "view.h"

namespace vtw {

std::string default_cache_dir();                         // "" if there is no home folder to put it in

// ---- radar pictures

constexpr int STORED_PICTURES = 4;                       // slots on disk
constexpr double STORED_MAX_AGE_SECONDS = 6 * 3600;      // older pictures are not worth loading

struct StoredPicture {
    RadarFrame frame;
    double unix_time = 0;                                // when it was fetched, seconds since 1970
};

// One picture as bytes: a signature, its area and size, the time, then the strengths run-length
// coded (most of a picture is no rain). decode_picture checks everything and says why it refused.
std::string encode_picture(const RadarFrame& frame, double unix_time);
bool decode_picture(const uint8_t* data, size_t size, StoredPicture& picture, std::string& error);

class RadarStore {
public:
    explicit RadarStore(std::string folder) : folder_(std::move(folder)) {}

    // The pictures on disk that are not too old, newest first. Unreadable files are skipped.
    std::vector<StoredPicture> load(double unix_now) const;

    // Keep a picture: it goes into the slot holding the oldest one (or an empty slot).
    bool save(const RadarFrame& frame, double unix_time, std::string& error);

private:
    std::string path_of(int slot) const;
    std::string folder_;
};

// ---- where you were looking

struct ViewState {
    GeoPoint centre;
    double zoom_km = 80;
    bool graticule = false;
    bool names = true;
};

bool load_view_state(const std::string& folder, ViewState& state);
bool save_view_state(const std::string& folder, const ViewState& state, std::string& error);

}  // namespace vtw
