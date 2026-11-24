#include "radar_feed.h"

#include <algorithm>
#include <cstdio>

#include "radar_source.h"

namespace vtw {

FetchSettings radar_fetch_settings(const Settings& settings) {
    FetchSettings fetch;
    fetch.user_agent = make_user_agent(settings.contact);
    return fetch;
}

RadarFeed::RadarFeed(const Settings& settings)
    : RadarFeed(radar_fetch_settings(settings), settings.refresh_minutes * 60.0, radar_url) {}

RadarFeed::RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for)
    : url_for_(std::move(url_for)), worker_(std::move(fetch)), refresh_seconds_(refresh_seconds) {}

void RadarFeed::update(const View& view, double now) {
    RadarResult result;
    if (worker_.take(result)) {
        scheduler_.finished(now, result.ok, result.retry_in);
        if (result.ok) {
            cache_.add(std::move(result.frame), now, now);
            error_.clear();
        } else {
            error_ = result.error;
            retry_at_ = scheduler_.not_before();
        }
    }

    // show the newest picture that covers the view; if none does, keep showing what there is
    const GeoBox wanted = view.box();
    if (std::shared_ptr<const CachedPicture> best = cache_.best_for(wanted, now)) current_ = best;
    else if (!current_) current_ = cache_.best_for(GeoBox{-90, -180, 90, 180}, now);

    if (!worker_.can_request()) return;
    const bool covered = current_ && covers(current_->frame.box, wanted);
    const bool stale = current_ && now - current_->fetched_at >= refresh_seconds_;
    if (!scheduler_.due(now, (bool)current_, covered, stale)) return;
    const GeoBox area = view.box(REQUEST_MARGIN);
    if (worker_.request({url_for_(area), area})) scheduler_.requested(now);
}

std::string RadarFeed::status(double now) const {
    char text[200];
    if (current_) {
        const int minutes = (int)((now - current_->fetched_at) / 60);
        if (minutes < 1) std::snprintf(text, sizeof text, "radar: picture just fetched");
        else std::snprintf(text, sizeof text, "radar: picture fetched %d min ago", minutes);
        std::string line = text;
        if (!error_.empty()) {
            std::snprintf(text, sizeof text, "; update failed (%s), next try in %d s", error_.c_str(),
                          (int)std::max(0.0, retry_at_ - now));
            line += text;
        }
        return line;
    }
    if (!error_.empty()) {
        std::snprintf(text, sizeof text, "radar: no picture yet (%s), next try in %d s", error_.c_str(),
                      (int)std::max(0.0, retry_at_ - now));
        return text;
    }
    return "radar: asking for a picture...";
}

}  // namespace vtw
