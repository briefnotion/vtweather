#include "radar_feed.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "radar_source.h"

namespace vtw {

FetchSettings radar_fetch_settings(const Settings& settings) {
    FetchSettings fetch;
    fetch.user_agent = make_user_agent(settings.contact);
    return fetch;
}

RadarFeed::RadarFeed(const Settings& settings, RadarStore* store, double unix_at_start)
    : RadarFeed(radar_fetch_settings(settings), settings.refresh_minutes * 60.0, radar_url, store, unix_at_start,
                radar_time_url()) {}

RadarFeed::RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for, RadarStore* store,
                     double unix_at_start, std::string time_url)
    : url_for_(std::move(url_for)), time_url_(std::move(time_url)), worker_(std::move(fetch)), refresh_seconds_(refresh_seconds),
      store_(store), unix_at_start_(unix_at_start) {
    if (!store_) return;
    std::vector<StoredPicture> saved = store_->load(unix_at_start_);     // newest first
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {           // oldest first, so the newest wins
        const GeoBox& b = it->frame.box;                                 // a picture that claims to lie outside the radar's area
        if (!covers(GeoBox{RADAR_COVERAGE.south - 1e-3, RADAR_COVERAGE.west - 1e-3, RADAR_COVERAGE.north + 1e-3,
                           RADAR_COVERAGE.east + 1e-3}, b))              // is damaged (older versions kept some): not used
            continue;
        cache_.add(std::move(it->frame), it->unix_time - unix_at_start_, 0, it->scan_unix);
    }
}

void RadarFeed::update(const View& view, double now) {
    RadarResult result;
    if (worker_.take(result)) {
        scheduler_.finished(now, result.ok, result.retry_in);
        if (result.ok) {
            const std::shared_ptr<const CachedPicture> kept = cache_.add(std::move(result.frame), now, now, result.scan_unix);
            if (store_) {
                std::string ignored;                              // a disk that cannot be written is no reason to stop
                store_->save(kept->frame, unix_at_start_ + now, kept->scan_unix, ignored);
            }
            error_.clear();
        } else {
            error_ = result.error;
            retry_at_ = scheduler_.not_before();
        }
    }

    // The radar covers only part of the earth. What is needed is the part of the view inside it; a view
    // wholly outside (the Arctic, Europe, the open Pacific) needs no picture and nothing is asked for.
    GeoBox wanted;
    outside_ = !intersect(clip_to_earth(view.box()), RADAR_COVERAGE, wanted);
    if (outside_) return;

    // show the newest picture that covers it; if none does, keep showing what there is
    const CachedPicture* before = current_.get();
    if (std::shared_ptr<const CachedPicture> best = cache_.best_for(wanted, now)) current_ = best;
    else if (!current_) current_ = cache_.best_for(RADAR_COVERAGE, now);
    if (current_.get() != before) revision_++;

    if (offline_ || !worker_.can_request()) return;
    const bool covered = current_ && covers(current_->frame.box, wanted);
    const bool stale = current_ && now - current_->fetched_at >= refresh_seconds_;
    if (!scheduler_.due(now, (bool)current_, covered, stale)) return;
    GeoBox area;                                          // a little more than the screen, inside the radar's area,
    if (!intersect(clip_to_earth(view.box(REQUEST_MARGIN)), RADAR_COVERAGE, area)) return;   // and so exactly what the picture shows
    if (worker_.request({url_for_(area), time_url_, area})) scheduler_.requested(now);
}

std::string RadarFeed::status(double now) const {
    char text[200];
    if (outside_) return "radar: none here (the US radar covers 20-55N, 130-60W)";
    // when the radar made the scan the picture shows (local time, and UTC), and how old that is
    auto scan_text = [&](const CachedPicture& p) -> std::string {
        if (p.scan_unix <= 0) return "";
        const std::time_t when = (std::time_t)p.scan_unix;
        std::tm local{}, utc{};
        localtime_r(&when, &local);
        gmtime_r(&when, &utc);
        const int minutes = std::max(0, (int)((unix_at_start_ + now - p.scan_unix) / 60));
        std::snprintf(text, sizeof text, "scan %02d:%02d (%02d:%02dZ), %d min old", local.tm_hour, local.tm_min,
                      utc.tm_hour, utc.tm_min, minutes);
        return text;
    };
    if (offline_) {
        if (!current_) return "radar: off (--offline), no saved picture";
        const std::string scan = scan_text(*current_);
        if (!scan.empty()) return "radar: off (--offline), saved picture, " + scan;
        const int minutes = (int)((now - current_->fetched_at) / 60);
        std::snprintf(text, sizeof text, "radar: off (--offline), saved picture from %d min ago", std::max(minutes, 0));
        return text;
    }
    if (current_) {
        const double age = now - current_->fetched_at;
        const int minutes = (int)(age / 60);
        std::string line;
        const std::string scan = scan_text(*current_);
        if (!scan.empty()) {
            line = "radar: " + scan;
        } else {
            if (minutes < 1) std::snprintf(text, sizeof text, "radar: picture just fetched");
            else std::snprintf(text, sizeof text, "radar: picture fetched %d min ago", minutes);
            line = text;
        }
        if (age >= refresh_seconds_) line += " (old)";
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
