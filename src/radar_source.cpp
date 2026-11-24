#include "radar_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

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

std::string radar_time_url() {
    return "https://opengeo.ncep.noaa.gov/geoserver/conus/ows?service=WMS&version=1.3.0"
           "&request=GetCapabilities&namespace=conus";
}

namespace {

// 2026-11-24T11:13:43Z (with or without fractions of a second) -> seconds since 1970 in UTC.
bool parse_iso_utc(const std::string& text, double& seconds) {
    int year, month, day, hour, minute;
    double second;
    char tail = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%lf%c", &year, &month, &day, &hour, &minute, &second, &tail) < 6) return false;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second < 0 || second >= 61) return false;
    if (year < 2000 || year > 2200) return false;
    std::tm parts{};
    parts.tm_year = year - 1900; parts.tm_mon = month - 1; parts.tm_mday = day;
    parts.tm_hour = hour; parts.tm_min = minute; parts.tm_sec = 0;
    const std::time_t whole = timegm(&parts);
    if (whole == (std::time_t)-1) return false;
    seconds = (double)whole + second;
    return true;
}

}  // namespace

bool parse_scan_time(const std::string& capabilities, const std::string& layer, ScanTime& scan) {
    scan = ScanTime();
    // the layer's own block, so that another layer's time is never taken
    const std::string marker = "<Name>" + layer + "</Name>";
    const size_t at = capabilities.find(marker);
    if (at == std::string::npos) return false;
    const size_t end = capabilities.find("</Layer>", at);
    const size_t dim = capabilities.find("<Dimension name=\"time\"", at);
    if (dim == std::string::npos || (end != std::string::npos && dim > end)) return false;
    const size_t tag_end = capabilities.find('>', dim);
    const size_t key = capabilities.find("default=\"", dim);
    if (tag_end == std::string::npos || key == std::string::npos || key > tag_end) return false;
    const size_t from = key + 9, to = capabilities.find('"', from);
    if (to == std::string::npos || to - from > 40) return false;
    const std::string iso = capabilities.substr(from, to - from);
    double seconds;
    if (!parse_iso_utc(iso, seconds)) return false;
    scan.unix_time = seconds;
    scan.iso = iso;
    return true;
}

std::string with_scan_time(const std::string& url, const std::string& iso) {
    return url + (url.find('?') == std::string::npos ? "?" : "&") + "time=" + iso;
}

}  // namespace vtw
