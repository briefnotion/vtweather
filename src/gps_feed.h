// GpsFeed: where the car is, read from a small JSON file that another program keeps up to date. On
// the Raspberry Pi that program is rasfled2, which writes /dev/shm/exchange/gps.json (a folder in
// memory) about once a second. vtweather only reads it: it never writes, and it never waits.
//
// What the file looks like (version 1; see rasfled2/source/gps_exchange.h, which writes it):
//
//   { "version": 1, "seq": 1234, "written_unix": 1764079402.123, "gps_time_unix": 1764079402.0,
//     "fix": true, "lat": 37.690, "lon": -97.340, "altitude_m": 412.0, "speed_kmph": 88.6,
//     "heading_deg": 87.0, "heading_valid": true, "satellites": 9, "hdop": 1.2 }
//
// The writer replaces the file whole (writes a new one and renames it over the old), so a read
// always sees a complete file; a damaged one is still handled, and reported.
//
// update() is called every frame and does almost nothing: about four times a second it looks at
// the file's modification time, and only when that changed does it read and parse the file. How
// fresh the data is comes from the file's age against the clock (both are this computer's clock),
// so a file left over from before, or from a program that has stopped, counts as lost at once.
//
// Linux only for now (the file's time comes from stat()).
#pragma once

#include <cstddef>
#include <string>

namespace vtw {

constexpr int GPS_FILE_VERSION = 1;               // the layout of the file this reads
constexpr double GPS_POLL_SECONDS = 0.25;         // how often the file is looked at
constexpr double GPS_STALE_SECONDS = 5.0;         // older than this, the data is lost (a fix comes about every second)
constexpr size_t GPS_MAX_FILE_BYTES = 16 * 1024;  // the file is about 300 bytes; a bigger one is not ours

struct GpsFix {
    bool fix = false;                // the position is good (when false, lat and lon are the last known)
    double lat = 0, lon = 0;         // degrees, north and east positive
    double altitude_m = 0;
    double speed_kmph = 0;
    double heading_deg = 0;          // direction of travel, 0 up to 360, true north
    bool heading_valid = false;      // moving, so the heading means something
    int satellites = 0;
    double hdop = 0;                 // under 2 is good
    double gps_time_unix = 0;        // the time of the fix by the satellites
    double written_unix = 0;         // when the file was written
    long long seq = 0;               // goes up with every write
};

// Read the text of a file into a GpsFix. False, with the reason in problem, if it is not valid JSON, has no
// supported version, lacks fix, lat or lon, or has a position that cannot be on the earth. The values that are
// only extra information are optional, and one of the wrong type is left at its default.
bool parse_gps_json(const std::string& text, GpsFix& fix, std::string& problem);

enum class GpsState {
    Off,        // no file is named in the settings
    NoFile,     // there is no file: the program that writes it is not running, or has not got a fix to tell yet
    Bad,        // the newest file could not be read (problem() says why)
    Stale,      // the file has not been written for GPS_STALE_SECONDS: the writer stopped, or the GPS was unplugged
    NoFix,      // the GPS is there but has no position yet
    Fix,        // a good position, fresh
};

class GpsFeed {
public:
    explicit GpsFeed(std::string path);              // "" turns it off

    // Look at the file if it is time. now_unix is the computer's clock, seconds since 1970.
    void update(double now_unix);

    GpsState state() const { return state_; }
    const GpsFix& fix() const { return fix_; }       // the newest good reading (all zeros before the first)
    bool has_data() const { return have_data_; }
    const std::string& problem() const { return problem_; }
    double age(double now_unix) const { return now_unix - mtime_; }    // since the file was written

private:
    void poll();
    void judge(double now_unix);

    std::string path_;
    bool polled_ = false;
    double last_poll_ = 0;

    bool file_seen_ = false;
    long long seen_sec_ = -1, seen_nsec_ = -1;       // the modification time that was read last
    double mtime_ = 0;
    bool have_data_ = false;
    bool bad_ = false;
    std::string problem_;
    GpsFix fix_;
    GpsState state_ = GpsState::NoFile;
};

// One line for the screen about the GPS, "" when there is nothing to say (off, or no file at all).
std::string gps_status_text(const GpsFeed& gps, double now_unix);

}  // namespace vtw
