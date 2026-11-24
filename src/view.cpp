#include "view.h"

#include <algorithm>
#include <cmath>

namespace vtw {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double EARTH_RADIUS_M = 6378137.0;       // the web map's sphere
constexpr double MEAN_RADIUS_KM = 6371.0088;       // for distances
double radians(double deg) { return deg * PI / 180.0; }
double degrees(double rad) { return rad * 180.0 / PI; }

// latitude, longitude -> Mercator metres
void project(GeoPoint p, double& x, double& y) {
    const double lat = std::min(std::max(p.lat, -MAX_LATITUDE), MAX_LATITUDE);   // the poles are infinitely far
    x = EARTH_RADIUS_M * radians(p.lon);
    y = EARTH_RADIUS_M * std::log(std::tan(PI / 4 + radians(lat) / 2));
}

// Mercator metres -> latitude, longitude
GeoPoint unproject(double x, double y) {
    GeoPoint p;
    p.lon = degrees(x / EARTH_RADIUS_M);
    p.lat = degrees(2 * std::atan(std::exp(y / EARTH_RADIUS_M)) - PI / 2);
    return p;
}

}  // namespace

void View::set_size(int cols, int rows) {
    cols_ = std::max(cols, 1);
    rows_ = std::max(rows, 1);
    refresh();
}

void View::set_centre(GeoPoint centre) {
    centre.lat = std::min(std::max(centre.lat, -MAX_LATITUDE), MAX_LATITUDE);
    centre.lon = std::remainder(centre.lon, 360.0);        // wrap into -180..180
    centre_ = centre;
    refresh();
}

void View::set_zoom_km(double km_across) {
    km_across_ = std::min(std::max(km_across, MIN_KM_ACROSS), MAX_KM_ACROSS);
    refresh();
}

void View::set_cell_aspect(double height_over_width) {
    aspect_ = std::min(std::max(height_over_width, 1.0), 3.0);
    refresh();
}

void View::set_heading(double degrees_) {
    heading_deg_ = degrees_;
    refresh();
}

void View::refresh() {
    project(centre_, cx_, cy_);
    // Mercator stretches distances by 1 / cos(latitude); undo that at the centre so that
    // km_across is real ground distance there.
    metres_per_col_ = km_across_ * 1000.0 / cols_ / std::cos(radians(centre_.lat));
    cos_h_ = std::cos(radians(heading_deg_));
    sin_h_ = std::sin(radians(heading_deg_));
}

GeoPoint View::at(double col, double row) const {
    const double right = (col - cols_ / 2.0) * metres_per_col_;              // on the screen
    const double up = -(row - rows_ / 2.0) * metres_per_col_ * aspect_;
    const double east = right * cos_h_ + up * sin_h_;                        // on the map
    const double north = -right * sin_h_ + up * cos_h_;
    return unproject(cx_ + east, cy_ + north);
}

void View::to_cell(GeoPoint place, double& col, double& row) const {
    double x, y;
    project(place, x, y);
    const double east = x - cx_;
    const double north = y - cy_;
    const double right = east * cos_h_ - north * sin_h_;
    const double up = east * sin_h_ + north * cos_h_;
    col = cols_ / 2.0 + right / metres_per_col_;
    row = rows_ / 2.0 - up / (metres_per_col_ * aspect_);
}

GeoBox View::box(double margin) const {
    const double dc = cols_ * margin, dr = rows_ * margin;
    const GeoPoint corner[4] = {at(-dc, -dr), at(cols_ + dc, -dr), at(-dc, rows_ + dr),
                                at(cols_ + dc, rows_ + dr)};
    GeoBox b{corner[0].lat, corner[0].lon, corner[0].lat, corner[0].lon};
    for (const GeoPoint& p : corner) {
        b.south = std::min(b.south, p.lat);
        b.north = std::max(b.north, p.lat);
        b.west = std::min(b.west, p.lon);
        b.east = std::max(b.east, p.lon);
    }
    return b;
}

void View::zoom(double factor) { set_zoom_km(km_across_ * factor); }

void View::pan_cells(double dcols, double drows) {
    set_centre(at(cols_ / 2.0 + dcols, rows_ / 2.0 + drows));
}

double distance_km(GeoPoint a, GeoPoint b) {
    const double dlat = radians(b.lat - a.lat), dlon = radians(b.lon - a.lon);
    const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(radians(a.lat)) * std::cos(radians(b.lat)) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * MEAN_RADIUS_KM * std::asin(std::sqrt(h));
}

}  // namespace vtw
