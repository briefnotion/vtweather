#include "radar_feed.h"

#include <algorithm>
#include <cstdio>

#include "radar_source.h"

namespace vtw {

namespace {

bool covers(const GeoBox& picture, const GeoBox& wanted) {
    return picture.south <= wanted.south && picture.north >= wanted.north &&
           picture.west <= wanted.west && picture.east >= wanted.east;
}

}  // namespace

FetchSettings radar_fetch_settings(const Settings& settings) {
    FetchSettings fetch;
    fetch.user_agent = make_user_agent(settings.contact);
    return fetch;
}

RadarFeed::RadarFeed(const Settings& settings)
    : RadarFeed(radar_fetch_settings(settings), settings.refresh_minutes * 60.0, radar_url) {}

RadarFeed::RadarFeed(FetchSettings fetch, double refresh_seconds, UrlBuilder url_for)
    : url_for_(std::move(url_for)), worker_(std::move(fetch)), scheduler_(refresh_seconds) {}

void RadarFeed::update(const View& view, double now) {
    RadarResult result;
    if (worker_.take(result)) {
        scheduler_.finished(now, result.ok, result.retry_in);
        if (result.ok) {
            frame_ = std::move(result.frame);
            have_ = true;
            fetched_at_ = now;
            error_.clear();
        } else {
            error_ = result.error;
            retry_at_ = scheduler_.not_before();
        }
    }
    if (!worker_.can_request()) return;

    const bool covered = have_ && covers(frame_.box, view.box());
    if (!scheduler_.due(now, have_, covered)) return;
    const GeoBox area = view.box(REQUEST_MARGIN);
    if (worker_.request({url_for_(area), area})) scheduler_.requested(now);
}

std::string RadarFeed::status(double now) const {
    char text[200];
    if (have_) {
        const int minutes = (int)((now - fetched_at_) / 60);
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
