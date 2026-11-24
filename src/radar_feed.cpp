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

RadarFeed::RadarFeed(const Settings& settings, RadarStore* store, double unix_at_start)
    : RadarFeed(radar_fetch_settings(settings), settings.refresh_minutes * 60.0, radar_url, store, unix_at_start) {}

RadarFeed::RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for, RadarStore* store,
                     double unix_at_start)
    : url_for_(std::move(url_for)), worker_(std::move(fetch)), refresh_seconds_(refresh_seconds),
      store_(store), unix_at_start_(unix_at_start) {
    if (!store_) return;
    std::vector<StoredPicture> saved = store_->load(unix_at_start_);     // newest first
    for (auto it = saved.rbegin(); it != saved.rend(); ++it)             // oldest first, so the newest wins
        cache_.add(std::move(it->frame), it->unix_time - unix_at_start_, 0);
}

void RadarFeed::update(const View& view, double now) {
    RadarResult result;
    if (worker_.take(result)) {
        scheduler_.finished(now, result.ok, result.retry_in);
        if (result.ok) {
            const std::shared_ptr<const CachedPicture> kept = cache_.add(std::move(result.frame), now, now);
            if (store_) {
                std::string ignored;                              // a disk that cannot be written is no reason to stop
                store_->save(kept->frame, unix_at_start_ + now, ignored);
            }
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

    if (offline_ || !worker_.can_request()) return;
    const bool covered = current_ && covers(current_->frame.box, wanted);
    const bool stale = current_ && now - current_->fetched_at >= refresh_seconds_;
    if (!scheduler_.due(now, (bool)current_, covered, stale)) return;
    const GeoBox area = view.box(REQUEST_MARGIN);
    if (worker_.request({url_for_(area), area})) scheduler_.requested(now);
}

std::string RadarFeed::status(double now) const {
    char text[200];
    if (offline_) {
        if (!current_) return "radar: off (--offline), no saved picture";
        const int minutes = (int)((now - current_->fetched_at) / 60);
        std::snprintf(text, sizeof text, "radar: off (--offline), saved picture from %d min ago", std::max(minutes, 0));
        return text;
    }
    if (current_) {
        const double age = now - current_->fetched_at;
        const int minutes = (int)(age / 60);
        if (minutes < 1) std::snprintf(text, sizeof text, "radar: picture just fetched");
        else std::snprintf(text, sizeof text, "radar: picture fetched %d min ago%s", minutes,
                           age >= refresh_seconds_ ? " (old)" : "");
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
