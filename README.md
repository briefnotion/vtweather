# vtweather

An overhead map of where you are, zoomable, with the real-time rain on it, drawn as
letters in a terminal. Built on [vtscreen](https://github.com/briefnotion/vtscreen).

**Status:** just started. It reads its settings, has the View that turns places on the earth
into places on the screen (zoom, pan, cell shape, ready for a rotated map), and shows a
graticule with a marker as a stand-in for the map. Planned, in this order: a polite web fetcher,
a PNG reader, one radar frame at a fixed place, zoom and pan keys, a base map, an animated loop
of the last hour or two, alerts.

## Build and run

Linux, `g++` with C++17 and `cmake` 3.16 or later. The libraries are git submodules, so
clone with `--recursive`:

    git clone --recursive https://github.com/briefnotion/vtweather.git
    cd vtweather
    cmake -B build
    cmake --build build
    ./build/vtweather             (q quits; also --no-color, --fps N, --cols N --rows N)

If `vtscreen/` or `third_party/json/` is empty, run `git submodule update --init`.

## Settings

`settings.sample.json` shows the settings. Copy it to `settings.json` and put in your own
place. `settings.json` is in `.gitignore`, so your location is never committed. The program
reads `settings.json` from the folder you run it in, else `settings.sample.json`, else uses
built-in defaults; `--settings FILE` names another file. A missing or damaged file never stops
it: the notes on screen say what was wrong and what was used instead.

| setting | meaning |
|---|---|
| `location` | `name`, `lat`, `lon`: where the map is centred, in degrees (north and east positive) |
| `zoom_km` | the ground distance across the screen, 2 to 6000 km |
| `cell_aspect` | how many times taller than wide a terminal cell is, 1 to 3 (2.0 suits most) |
| `refresh_minutes` | how often to ask for new radar, never less than 5: the radar updates about that often and the data comes from a free service |
| `user_agent_contact` | optional, how a data service can reach you; sent in the User-Agent |

## Tests

    cmake --build build
    ctest --test-dir build --output-on-failure

They cover the View (scale, cell shape, north up, rotation, round trips, pan, zoom) and the
settings loader (good, missing, damaged, wrong type, out of range).

## Third-party code

- [vtscreen](https://github.com/briefnotion/vtscreen) (MIT), the drawing library, in `vtscreen/`.
- [nlohmann/json](https://github.com/nlohmann/json) (MIT), in `third_party/json/`, pinned to
  release v3.12.0.

## Licence

GNU General Public License v3.0, see `LICENSE`. The libraries it uses, vtscreen and nlohmann/json,
are MIT-licensed and keep their own licences in their folders.
