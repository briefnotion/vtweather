#!/bin/sh
# Download the Natural Earth map files that the base map is made from (public domain,
# https://www.naturalearthdata.com). They are plain GeoJSON, about 18 MB in all, kept in
# tools/natural_earth/ (not in the repo). This is only needed to remake data/basemap.bin.
#
# Kind to the host: one file at a time, 5 seconds apart, each fetched only if it is missing,
# and the request says who is asking.
set -e
cd "$(dirname "$0")"
mkdir -p natural_earth
cd natural_earth

AGENT="vtweather-data/0.1 (+https://github.com/briefnotion/vtweather)"
BASE="https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson"
FILES="ne_110m_land ne_110m_lakes ne_50m_land ne_50m_lakes ne_10m_land ne_10m_lakes"

first=1
for name in $FILES; do
    if [ -s "$name.geojson" ]; then echo "have $name"; continue; fi
    [ "$first" = 1 ] || sleep 5
    first=0
    echo "getting $name ..."
    curl -fsS --max-time 300 -A "$AGENT" -o "$name.geojson.part" "$BASE/$name.geojson"
    mv "$name.geojson.part" "$name.geojson"
done
echo "done"
