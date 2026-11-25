// Settings: where you are and a few options, read from a JSON file.
//
// The file is settings.json (your own, never committed) or, if there is none,
// settings.sample.json. --settings PATH names another file. A missing or damaged
// file never stops the program: the built-in defaults are used and `notes` says why.
#pragma once

#include <string>

#include "view.h"

namespace vtw {

// The radar is updated about every 5 minutes, so asking more often only adds load to
// a free service. Whatever the file says, the refresh is never shorter than this.
constexpr int MIN_REFRESH_MINUTES = 5;

// Where the GPS position is read from (see gps_feed.h): a file in memory that rasfled2 keeps up to date.
constexpr const char* DEFAULT_GPS_FILE = "/dev/shm/exchange/gps.json";

struct Settings {
    std::string place_name = "Wichita, Kansas (sample)";
    GeoPoint home{37.69, -97.34};
    double zoom_km = 80;              // ground distance across the screen
    double cell_aspect = 2.0;         // how many times taller than wide a terminal cell is
    int refresh_minutes = MIN_REFRESH_MINUTES;
    std::string contact;              // for the User-Agent, optional
    bool remember_view = true;        // start where you were looking last time (r still goes home)
    std::string cache_dir;            // where radar pictures and the last view are kept; "" = the usual place
    std::string gps_file = DEFAULT_GPS_FILE;   // the file the GPS position is read from; "" = no GPS
    std::string source = "built-in defaults";   // the file the settings came from
};

// Read the settings. path "" means: settings.json, else settings.sample.json, else the
// defaults. Anything odd (a missing file, a wrong type, a value out of range) is
// described in notes, one line each, and the default or the nearest allowed value is used.
Settings load_settings(const std::string& path, std::string& notes);

// Find an option such as "--settings PATH" in the command line, remove it from argv (so the library's own
// option reader does not see it) and return the path, or "" if it was not given.
std::string take_settings_option(int& argc, char** argv, const char* option = "--settings");

// Find a flag such as "--offline" in the command line, remove it from argv and say whether it
// was there.
bool take_flag(int& argc, char** argv, const char* flag);

}  // namespace vtw
