// gps_sim - a pretend GPS for trying vtweather without a car. It drives round a block, over and over, and
// writes the same JSON file that rasfled2 writes (see src/gps_feed.h), once a second, so vtweather sees a car
// pulling away, cruising, turning, stopping at a light (where the GPS wanders), creeping, and going through a
// tunnel. See tools/gps_sim_route.h for the drive.
//
//   ./build/gps_sim                          start where settings.json says home is
//   ./build/gps_sim --lat 47.6 --lon -122.3  or start somewhere else
//   ./build/gps_sim --warp 20                cover 20 times the distance (to cross a zoomed-out map; the speed stays real)
//
// Options: --file PATH (default /dev/shm/exchange/gps.json, the same as vtweather's gps_file), --settings FILE
// (where to read the starting place from), --lat D --lon D, --heading D (0 = north, 90 = east, the default),
// --warp N, --hz N (writes a second, 1 by default), --no-tunnel, --keep (leave the file when it quits), --force.
// Ctrl-C quits, and by default removes the file.
//
// Do not run it while rasfled2 is writing the real GPS file: it stops if the file was written in the last few seconds,
// unless you give --force.

#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "gps_sim_route.h"
#include "src/settings.h"

namespace {

std::atomic<bool> g_quit(false);
void on_signal(int) { g_quit = true; }

double now_unix() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

bool number_after(int argc, char** argv, const char* option, double& out) {
    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) == option) { out = std::atof(argv[i + 1]); return true; }
    }
    return false;
}

}  // namespace

#ifndef GPS_SIM_NO_MAIN
int main(int argc, char** argv) {
    std::string path = vtw::DEFAULT_GPS_FILE;
    const std::string given = vtw::take_settings_option(argc, argv, "--file");
    if (!given.empty()) path = given;
    const std::string settings_path = vtw::take_settings_option(argc, argv);
    const bool keep = vtw::take_flag(argc, argv, "--keep");
    const bool force = vtw::take_flag(argc, argv, "--force");
    const bool tunnel = !vtw::take_flag(argc, argv, "--no-tunnel");

    std::string notes;
    const vtw::Settings settings = vtw::load_settings(settings_path, notes);
    double lat = settings.home.lat, lon = settings.home.lon, heading = 90, warp = 1, hz = 1;
    number_after(argc, argv, "--lat", lat);
    number_after(argc, argv, "--lon", lon);
    number_after(argc, argv, "--heading", heading);
    number_after(argc, argv, "--warp", warp);
    number_after(argc, argv, "--hz", hz);
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || warp < 0.1 || warp > 1000 || hz < 0.2 || hz > 20) {
        std::fprintf(stderr, "gps_sim: lat must be -90..90, lon -180..180, warp 0.1..1000, hz 0.2..20\n");
        return 2;
    }

    // someone else is writing the file: do not fight them
    struct stat info;
    if (!force && ::stat(path.c_str(), &info) == 0) {
        const double age = now_unix() - ((double)info.st_mtim.tv_sec + (double)info.st_mtim.tv_nsec * 1e-9);
        if (age < 3.0 && age > -3.0) {
            std::fprintf(stderr, "gps_sim: %s was written %.1f s ago, so something else is writing it (rasfled2?).\n"
                                 "         Not touching it; stop that first, or use --force.\n", path.c_str(), age);
            return 1;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::printf("gps_sim: writing %s, %.1f times a second, from %.4f %.4f (Ctrl-C to quit)\n", path.c_str(), hz, lat, lon);
    if (warp != 1) std::printf("gps_sim: distances are %.1f times real; the speed written is real\n", warp);

    const double dt = 1.0 / hz;
    vtw::sim::DriveSim sim(lat, lon, heading, warp, (unsigned)std::chrono::steady_clock::now().time_since_epoch().count(), tunnel, now_unix());
    unsigned long long seq = 0;
    auto next = std::chrono::steady_clock::now();
    bool failed_before = false;
    while (!g_quit) {
        const vtw::sim::Sample s = sim.step(dt);
        if (s.write) {
            seq++;
            if (!vtw::sim::write_gps_file(path, s, seq, now_unix())) {
                if (!failed_before) std::fprintf(stderr, "gps_sim: could not write %s\n", path.c_str());
                failed_before = true;
            }
        }
        std::printf("%-30s %s  %5.1f mph  heading %5.1f%s  %.5f %.5f%s\n", s.phase, s.fix ? "fix   " : "no fix", s.speed_kmph * 0.621371,
                    s.heading_deg, s.heading_valid ? "" : "?", s.lat, s.lon, s.write ? "" : "  (nothing written)");
        std::fflush(stdout);
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(dt));
        while (!g_quit && std::chrono::steady_clock::now() < next)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!keep) {
        std::remove(path.c_str());
        std::remove((path + ".tmp").c_str());
    }
    std::printf("gps_sim: stopped%s\n", keep ? "" : ", file removed");
    return 0;
}
#endif
