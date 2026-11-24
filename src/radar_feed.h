// RadarFeed: the radar picture for the screen, kept up to date. It owns the worker thread, the
// scheduler and a cache of recent pictures: once a frame the main loop calls update(), which takes
// in a new picture if one has arrived, picks the newest cached picture that covers the view (so
// going back to a place you just left needs no download) and sends a request if the scheduler says
// it is time. It never waits. frame() is the picture to draw, and status() is one line to show the
// person how it is going.
#pragma once

#include <functional>
#include <string>

#include <memory>

#include "radar_cache.h"
#include "radar_frame.h"
#include "radar_scheduler.h"
#include "radar_worker.h"
#include "settings.h"
#include "store.h"
#include "view.h"

namespace vtw {

// How the picture of an area is asked for (the tests put their own server here).
using UrlBuilder = std::function<std::string(const GeoBox&)>;

// Ask for a picture a little larger than the screen, so small pans need no new request.
constexpr double REQUEST_MARGIN = 0.25;

FetchSettings radar_fetch_settings(const Settings& settings);     // who we are when we ask

class RadarFeed {
public:
    // store, if there is one, holds pictures between runs: its pictures are loaded now and each new one
    // is saved. unix_at_start is the clock time (seconds since 1970) of time 0 of update().
    RadarFeed(const Settings& settings, RadarStore* store, double unix_at_start);
    RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for, RadarStore* store = nullptr,
              double unix_at_start = 0);

    void set_offline(bool offline) { offline_ = offline; }     // show what is held, ask for nothing

    void update(const View& view, double now);

    bool has_frame() const { return (bool)current_; }
    const RadarFrame& frame() const { return current_ ? current_->frame : none_; }
    size_t cached_pictures() const { return cache_.size(); }
    std::string status(double now) const;
    bool failing() const { return !error_.empty(); }       // the latest attempt did not work

private:
    UrlBuilder url_for_;
    RadarWorker worker_;
    RadarScheduler scheduler_;
    double refresh_seconds_;
    RadarCache cache_;
    std::shared_ptr<const CachedPicture> current_;      // the picture being shown
    RadarFrame none_;
    double retry_at_ = 0;
    RadarStore* store_;
    double unix_at_start_;
    bool offline_ = false;
    std::string error_;
};

}  // namespace vtw
