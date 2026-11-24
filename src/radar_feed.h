// RadarFeed: the radar picture for the screen, kept up to date. It owns the worker thread and
// the scheduler: once a frame the main loop calls update(), which takes in a new picture if one
// has arrived and sends a request if the scheduler says it is time. It never waits. frame() is
// the newest picture, and status() is one line to show the person how it is going.
#pragma once

#include <functional>
#include <string>

#include "radar_frame.h"
#include "radar_scheduler.h"
#include "radar_worker.h"
#include "settings.h"
#include "view.h"

namespace vtw {

// How the picture of an area is asked for (the tests put their own server here).
using UrlBuilder = std::function<std::string(const GeoBox&)>;

// Ask for a picture a little larger than the screen, so small pans need no new request.
constexpr double REQUEST_MARGIN = 0.25;

FetchSettings radar_fetch_settings(const Settings& settings);     // who we are when we ask

class RadarFeed {
public:
    explicit RadarFeed(const Settings& settings);
    RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for);

    void update(const View& view, double now);

    bool has_frame() const { return have_; }
    const RadarFrame& frame() const { return frame_; }
    std::string status(double now) const;
    bool failing() const { return !error_.empty(); }       // the latest attempt did not work

private:
    UrlBuilder url_for_;
    RadarWorker worker_;
    RadarScheduler scheduler_;
    RadarFrame frame_;
    bool have_ = false;
    double fetched_at_ = 0;
    double retry_at_ = 0;
    std::string error_;
};

}  // namespace vtw
