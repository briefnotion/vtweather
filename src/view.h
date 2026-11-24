// View: where the screen is looking on the map, and how to go between a place on the
// earth (latitude, longitude) and a place on the screen (a position in cells).
//
// The map is the usual web map: Mercator, north up, so shapes keep their look at every
// zoom. The zoom is given as the ground distance across the screen at its centre
// (km_across), which does not depend on how many columns the terminal has. A terminal
// cell is taller than it is wide (cell_aspect), and that is allowed for, so a circle on
// the ground stays a circle on the screen.
//
// Cell positions are continuous: the top left corner of cell (0, 0) is (0, 0) and the
// middle of cell (col, row) is (col + 0.5, row + 0.5).
//
// The view can be turned so that any compass direction is up the screen (set_heading).
// Everything below goes through that one turn, so a heading-up map needs no other change;
// for now the heading is 0 (north up) and nothing sets it.
#pragma once

namespace vtw {

struct GeoPoint {
    double lat = 0;      // degrees, north is positive
    double lon = 0;      // degrees, east is positive
};

struct GeoBox {
    double south = 0, west = 0, north = 0, east = 0;     // degrees
};

constexpr double MAX_LATITUDE = 85.0;       // Mercator cannot show the poles
constexpr double MIN_KM_ACROSS = 2.0;       // the closest zoom
constexpr double MAX_KM_ACROSS = 6000.0;    // the widest zoom

class View {
public:
    void set_size(int cols, int rows);            // the picture area, in cells
    void set_centre(GeoPoint centre);
    void set_zoom_km(double km_across);           // clamped to the zoom limits
    void set_cell_aspect(double height_over_width);
    void set_heading(double degrees);             // the compass direction that is up

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    GeoPoint centre() const { return centre_; }
    double zoom_km() const { return km_across_; }
    double km_per_col() const { return km_across_ / cols_; }      // at the centre

    GeoPoint at(double col, double row) const;                    // the earth under a position
    void to_cell(GeoPoint place, double& col, double& row) const; // the position of a place
    GeoBox box(double margin = 0) const;      // the area on screen, grown by margin (0.2 = 20 %)

    void zoom(double factor);                 // 0.5 shows half the distance: zooms in
    void pan_cells(double dcols, double drows);   // move the view, in cells as seen on screen

private:
    void refresh();                           // recompute the numbers below

    int cols_ = 100, rows_ = 30;
    GeoPoint centre_{37.69, -97.34};
    double km_across_ = 80;
    double aspect_ = 2.0;
    double heading_deg_ = 0;

    double cx_ = 0, cy_ = 0;                  // the centre in Mercator metres
    double metres_per_col_ = 1;               // Mercator metres across one column
    double cos_h_ = 1, sin_h_ = 0;            // the turn
};

// The great-circle distance between two places, in km.
double distance_km(GeoPoint a, GeoPoint b);

}  // namespace vtw
