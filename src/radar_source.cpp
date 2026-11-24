#include "radar_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vtw {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double KM_PER_DEGREE = 111.195;          // along a meridian

GeoBox clipped(GeoBox b) {
    b.south = std::min(std::max(b.south, -90.0), 90.0);
    b.north = std::min(std::max(b.north, -90.0), 90.0);
    b.west = std::min(std::max(b.west, -180.0), 180.0);
    b.east = std::min(std::max(b.east, -180.0), 180.0);
    return b;
}

int side(double km) {
    return std::min(std::max((int)std::lround(km / RADAR_KM_PER_PIXEL), MIN_PICTURE_SIDE), MAX_PICTURE_SIDE);
}

}  // namespace

void radar_picture_size(const GeoBox& box, int& width, int& height) {
    const GeoBox b = clipped(box);
    const double middle = (b.south + b.north) / 2 * PI / 180;
    width = side((b.east - b.west) * KM_PER_DEGREE * std::cos(middle));
    height = side((b.north - b.south) * KM_PER_DEGREE);
}

std::string radar_url(const GeoBox& box) {
    const GeoBox b = clipped(box);
    int width, height;
    radar_picture_size(b, width, height);
    char url[512];
    std::snprintf(url, sizeof url,
        "https://opengeo.ncep.noaa.gov/geoserver/ows?service=WMS&version=1.1.1&request=GetMap"
        "&layers=conus:conus_bref_qcd&styles=&srs=EPSG:4326&bbox=%.5f,%.5f,%.5f,%.5f"
        "&width=%d&height=%d&format=image/png;%%20mode=8bit&transparent=true",
        b.west, b.south, b.east, b.north, width, height);
    return url;
}

}  // namespace vtw
