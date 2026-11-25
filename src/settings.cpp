#include "settings.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vtw {

namespace {

using Json = nlohmann::json;

bool file_exists(const std::string& path) { return std::ifstream(path).good(); }

// Read key into out if it is there. A value of the wrong type is left at its default
// and described in notes.
template <class T>
void read_key(const Json& object, const char* key, T& out, std::string& notes) {
    if (!object.contains(key)) return;
    try {
        object.at(key).get_to(out);
    } catch (const Json::exception&) {
        notes += std::string("'") + key + "' has the wrong type: using the default\n";
    }
}

// Keep value inside [low, high]; say so in notes if it had to move.
template <class T>
void keep_between(T& value, T low, T high, const char* name, std::string& notes) {
    const T before = value;
    value = std::min(std::max(value, low), high);
    if (value != before) {
        std::ostringstream line;
        line << "'" << name << "' = " << before << " is out of range: using " << value << "\n";
        notes += line.str();
    }
}

void read_settings(const Json& json, Settings& s, std::string& notes) {
    if (json.contains("location") && json["location"].is_object()) {
        const Json& place = json["location"];
        read_key(place, "name", s.place_name, notes);
        read_key(place, "lat", s.home.lat, notes);
        read_key(place, "lon", s.home.lon, notes);
    }
    read_key(json, "zoom_km", s.zoom_km, notes);
    read_key(json, "cell_aspect", s.cell_aspect, notes);
    read_key(json, "refresh_minutes", s.refresh_minutes, notes);
    read_key(json, "user_agent_contact", s.contact, notes);
    read_key(json, "remember_view", s.remember_view, notes);
    read_key(json, "cache_dir", s.cache_dir, notes);
    read_key(json, "gps_file", s.gps_file, notes);
    read_key(json, "moving_mph", s.moving_mph, notes);

    keep_between(s.home.lat, -MAX_CENTRE_LATITUDE, MAX_CENTRE_LATITUDE, "lat", notes);
    keep_between(s.home.lon, -180.0, 180.0, "lon", notes);
    keep_between(s.zoom_km, MIN_KM_ACROSS, MAX_KM_ACROSS, "zoom_km", notes);
    keep_between(s.cell_aspect, 1.0, 3.0, "cell_aspect", notes);
    keep_between(s.refresh_minutes, MIN_REFRESH_MINUTES, 24 * 60, "refresh_minutes", notes);
    keep_between(s.moving_mph, 1.0, 30.0, "moving_mph", notes);
}

}  // namespace

Settings load_settings(const std::string& path, std::string& notes) {
    Settings settings;
    std::string file = path;
    if (file.empty()) {
        if (file_exists("settings.json")) file = "settings.json";
        else if (file_exists("settings.sample.json")) file = "settings.sample.json";
    } else if (!file_exists(file)) {
        notes += "settings file '" + file + "' not found: using the defaults\n";
        file.clear();
    }
    if (file.empty()) return settings;

    std::ifstream in(file);
    const Json json = Json::parse(in, nullptr, false);       // false: no exception
    if (json.is_discarded() || !json.is_object()) {
        notes += "settings file '" + file + "' is not valid JSON: using the defaults\n";
        return settings;
    }
    settings.source = file;
    read_settings(json, settings, notes);
    return settings;
}

std::string take_settings_option(int& argc, char** argv, const char* option) {
    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) != option) continue;
        const std::string path = argv[i + 1];
        for (int j = i; j + 2 < argc; j++) argv[j] = argv[j + 2];
        argc -= 2;
        return path;
    }
    return "";
}

bool take_flag(int& argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) != flag) continue;
        for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1];
        argc -= 1;
        return true;
    }
    return false;
}

}  // namespace vtw
