// RadarSource: how to ask NOAA's radar map service for a picture of an area. The service is
// a standard web map server (WMS): give it a box in latitude and longitude and a size in
// pixels, and it draws the radar for that box (the national 1 km composite, which is updated
// about every 5 minutes) as a PNG with a transparent background.
#pragma once

#include <string>

#include "view.h"

namespace vtw {

// The radar's own resolution is about 1 km per pixel, so a picture needs no more than that.
constexpr double RADAR_KM_PER_PIXEL = 1.0;
constexpr int MIN_PICTURE_SIDE = 64;
constexpr int MAX_PICTURE_SIDE = 1500;         // keeps the answer small, however far out the view is

// The area the radar covers: the continental United States and a margin (the service's own
// layer). Anywhere else a request would only bring back an empty picture.
constexpr GeoBox RADAR_COVERAGE{20.0, -130.0, 55.0, -60.0};

// The box cut to the real earth (latitude -90..90, longitude -180..180).
GeoBox clip_to_earth(const GeoBox& box);

// The area two boxes share, in out; false if they share none worth a picture.
bool intersect(const GeoBox& a, const GeoBox& b, GeoBox& out);

// How many pixels wide and high a picture of this area should be: about one per kilometre,
// within the limits above.
void radar_picture_size(const GeoBox& box, int& width, int& height);

// The web address that asks for a picture of the area. The box is clipped to the real earth.
std::string radar_url(const GeoBox& box);

// The service describes its radar layer, including the time of the newest scan, in a small
// document (about 18 KB). The program asks for it just before a picture, so that it can ask for the
// picture of exactly that scan and know when the radar was really looking.
std::string radar_time_url();

struct ScanTime {
    double unix_time = 0;        // seconds since 1970 (UTC)
    std::string iso;             // as the service writes it, such as 2026-11-24T11:13:43Z
};

// The newest scan time of a layer in the service's capabilities document (the default of its
// "time" dimension). False if the layer or its time is not there or cannot be read.
bool parse_scan_time(const std::string& capabilities, const std::string& layer, ScanTime& scan);

// The layer the program draws, as the service names it in its capabilities.
constexpr const char* RADAR_LAYER = "conus_bref_qcd";

// A picture address with the time of the scan to draw added to it.
std::string with_scan_time(const std::string& url, const std::string& iso);

}  // namespace vtw
