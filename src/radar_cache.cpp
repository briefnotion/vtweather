#include "radar_cache.h"

#include <algorithm>

namespace vtw {

bool covers(const GeoBox& picture, const GeoBox& wanted) {
    return picture.south <= wanted.south && picture.north >= wanted.north && picture.west <= wanted.west &&
           picture.east >= wanted.east;
}

std::shared_ptr<const CachedPicture> RadarCache::add(RadarFrame frame, double fetched_at, double now, double scan_unix) {
    auto picture = std::make_shared<CachedPicture>();
    picture->frame = std::move(frame);
    picture->fetched_at = fetched_at;
    picture->scan_unix = scan_unix;
    picture->used_at = now;

    // anything older that this picture covers is no use any more
    pictures_.erase(std::remove_if(pictures_.begin(), pictures_.end(),
                                   [&](const std::shared_ptr<CachedPicture>& old) {
                                       return old->fetched_at <= fetched_at && covers(picture->frame.box, old->frame.box);
                                   }),
                    pictures_.end());
    pictures_.push_back(picture);
    while (pictures_.size() > limit_) {                         // the one used longest ago goes (never the new one)
        auto oldest = std::min_element(pictures_.begin(), pictures_.end() - 1,
                                       [](const std::shared_ptr<CachedPicture>& a, const std::shared_ptr<CachedPicture>& b) {
                                           return a->used_at < b->used_at;
                                       });
        pictures_.erase(oldest);
    }
    return picture;
}

std::shared_ptr<const CachedPicture> RadarCache::best_for(const GeoBox& area, double now) {
    std::shared_ptr<CachedPicture> best;
    for (const std::shared_ptr<CachedPicture>& p : pictures_)
        if (covers(p->frame.box, area) && (!best || p->fetched_at > best->fetched_at)) best = p;
    if (best) best->used_at = now;
    return best;
}

}  // namespace vtw
