#include "store.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vtw {

namespace fs = std::filesystem;

namespace {

constexpr char PICTURE_MAGIC[8] = {'V', 'T', 'W', 'R', 'A', 'D', '1', '\n'};
constexpr int MAX_SIDE = 8192;
constexpr size_t MAX_PIXELS = 32u * 1024 * 1024;

void put64(std::string& s, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    for (int i = 0; i < 8; i++) s += (char)((bits >> (8 * i)) & 0xFF);
}
void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF);
}

struct Reader {
    const uint8_t* data;
    size_t size, pos = 0;
    bool bad = false;
    double f64() {
        if (size - pos < 8) { bad = true; return 0; }
        uint64_t bits = 0;
        for (int i = 0; i < 8; i++) bits |= (uint64_t)data[pos + i] << (8 * i);
        pos += 8;
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    uint32_t u32() {
        if (size - pos < 4) { bad = true; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)data[pos + i] << (8 * i);
        pos += 4;
        return v;
    }
};

bool write_file_atomically(const std::string& path, const std::string& bytes, std::string& error) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string temporary = path + ".part";
    {
        std::ofstream out(temporary, std::ios::binary);
        if (!out || !out.write(bytes.data(), (std::streamsize)bytes.size())) {
            error = "cannot write " + temporary;
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) { error = "cannot move " + temporary + " into place"; return false; }
    return true;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream bytes;
    bytes << in.rdbuf();
    return bytes.str();
}

}  // namespace

std::string default_cache_dir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
        if (*xdg) return std::string(xdg) + "/vtweather";
    if (const char* home = std::getenv("HOME"))
        if (*home) return std::string(home) + "/.cache/vtweather";
    return "";
}

std::string encode_picture(const RadarFrame& frame, double unix_time) {
    std::string s(PICTURE_MAGIC, sizeof PICTURE_MAGIC);
    put64(s, unix_time);
    put64(s, frame.box.south); put64(s, frame.box.west); put64(s, frame.box.north); put64(s, frame.box.east);
    put32(s, (uint32_t)frame.width);
    put32(s, (uint32_t)frame.height);
    for (size_t i = 0; i < frame.dbz.size();) {               // runs: a value and how many times it repeats
        size_t run = 1;
        while (i + run < frame.dbz.size() && frame.dbz[i + run] == frame.dbz[i] && run < 65535) run++;
        s += (char)frame.dbz[i];
        s += (char)(run & 0xFF);
        s += (char)(run >> 8);
        i += run;
    }
    return s;
}

bool decode_picture(const uint8_t* data, size_t size, StoredPicture& picture, std::string& error) {
    picture = StoredPicture();
    auto fail = [&](const char* why) { error = why; picture = StoredPicture(); return false; };
    if (size < sizeof PICTURE_MAGIC || std::memcmp(data, PICTURE_MAGIC, sizeof PICTURE_MAGIC) != 0) return fail("not a stored picture");
    Reader in{data, size, sizeof PICTURE_MAGIC};
    picture.unix_time = in.f64();
    picture.frame.box.south = in.f64(); picture.frame.box.west = in.f64();
    picture.frame.box.north = in.f64(); picture.frame.box.east = in.f64();
    const uint32_t width = in.u32(), height = in.u32();
    if (in.bad) return fail("the stored picture is cut off");
    if (width == 0 || height == 0 || width > (uint32_t)MAX_SIDE || height > (uint32_t)MAX_SIDE || (size_t)width * height > MAX_PIXELS)
        return fail("the stored picture has an impossible size");
    if (!std::isfinite(picture.unix_time) || !(picture.frame.box.east > picture.frame.box.west) ||
        !(picture.frame.box.north > picture.frame.box.south) || !std::isfinite(picture.frame.box.south + picture.frame.box.west))
        return fail("the stored picture has an impossible area");
    const size_t total = (size_t)width * height;
    picture.frame.width = (int)width;
    picture.frame.height = (int)height;
    picture.frame.dbz.reserve(total);
    while (picture.frame.dbz.size() < total) {
        if (size - in.pos < 3) return fail("the stored picture is cut off");
        const uint8_t value = data[in.pos];
        const size_t run = (size_t)data[in.pos + 1] | ((size_t)data[in.pos + 2] << 8);
        in.pos += 3;
        if (run == 0 || picture.frame.dbz.size() + run > total) return fail("the stored picture is damaged");
        picture.frame.dbz.insert(picture.frame.dbz.end(), run, value);
    }
    if (in.pos != size) return fail("the stored picture is damaged");
    return true;
}

std::string RadarStore::path_of(int slot) const { return folder_ + "/radar_" + std::to_string(slot) + ".bin"; }

std::vector<StoredPicture> RadarStore::load(double unix_now) const {
    std::vector<StoredPicture> pictures;
    for (int slot = 0; slot < STORED_PICTURES; slot++) {
        const std::string bytes = read_file(path_of(slot));
        StoredPicture picture;
        std::string error;
        if (bytes.empty() || !decode_picture((const uint8_t*)bytes.data(), bytes.size(), picture, error)) continue;
        if (unix_now - picture.unix_time > STORED_MAX_AGE_SECONDS || picture.unix_time > unix_now + 600) continue;
        pictures.push_back(std::move(picture));
    }
    std::sort(pictures.begin(), pictures.end(), [](const StoredPicture& a, const StoredPicture& b) { return a.unix_time > b.unix_time; });
    return pictures;
}

bool RadarStore::save(const RadarFrame& frame, double unix_time, std::string& error) {
    int target = 0;
    double oldest = 1e300;
    for (int slot = 0; slot < STORED_PICTURES; slot++) {            // an empty or unreadable slot, else the oldest
        const std::string bytes = read_file(path_of(slot));
        StoredPicture existing;
        std::string ignored;
        const double age_key = (!bytes.empty() && decode_picture((const uint8_t*)bytes.data(), bytes.size(), existing, ignored))
                                   ? existing.unix_time : -1e300;
        if (age_key < oldest) { oldest = age_key; target = slot; }
    }
    return write_file_atomically(path_of(target), encode_picture(frame, unix_time), error);
}

bool load_view_state(const std::string& folder, ViewState& state) {
    const std::string text = read_file(folder + "/view.json");
    if (text.empty()) return false;
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return false;
    try {
        ViewState s;
        s.centre.lat = json.at("lat").get<double>();
        s.centre.lon = json.at("lon").get<double>();
        s.zoom_km = json.at("zoom_km").get<double>();
        if (json.contains("graticule")) s.graticule = json["graticule"].get<bool>();
        if (json.contains("names")) s.names = json["names"].get<bool>();
        if (!std::isfinite(s.centre.lat) || !std::isfinite(s.centre.lon) || !std::isfinite(s.zoom_km)) return false;
        if (std::fabs(s.centre.lat) > 90 || std::fabs(s.centre.lon) > 360) return false;
        state = s;
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool save_view_state(const std::string& folder, const ViewState& state, std::string& error) {
    nlohmann::json json;
    json["lat"] = state.centre.lat;
    json["lon"] = state.centre.lon;
    json["zoom_km"] = state.zoom_km;
    json["graticule"] = state.graticule;
    json["names"] = state.names;
    return write_file_atomically(folder + "/view.json", json.dump(2) + "\n", error);
}

}  // namespace vtw
