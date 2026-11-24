// RadarCache: the recent radar pictures, kept in memory so that looking somewhere you looked a
// moment ago needs no new download. Each picture is kept with the time it was fetched. For a
// view the cache gives the newest picture that covers it. A new picture replaces any older one
// whose area it covers (nothing is kept twice), and when there are more than the limit the one
// used longest ago goes.
#pragma once

#include <memory>
#include <vector>

#include "radar_frame.h"

namespace vtw {

constexpr size_t RADAR_CACHE_PICTURES = 8;       // up to about 2 MB each, at the largest size

struct CachedPicture {
    RadarFrame frame;
    double fetched_at = 0;       // seconds on the program's clock (negative: from before it started)
    double used_at = 0;          // when it was last looked up
};

bool covers(const GeoBox& picture, const GeoBox& wanted);

class RadarCache {
public:
    explicit RadarCache(size_t limit = RADAR_CACHE_PICTURES) : limit_(limit) {}

    // Keep a picture; returns it as stored. Older pictures it covers are dropped.
    std::shared_ptr<const CachedPicture> add(RadarFrame frame, double fetched_at, double now);

    // The newest picture that covers the area, or null.
    std::shared_ptr<const CachedPicture> best_for(const GeoBox& area, double now);

    size_t size() const { return pictures_.size(); }
    const std::vector<std::shared_ptr<CachedPicture>>& all() const { return pictures_; }

private:
    size_t limit_;
    std::vector<std::shared_ptr<CachedPicture>> pictures_;
};

}  // namespace vtw
