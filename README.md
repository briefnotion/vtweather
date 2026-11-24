# vtweather

An overhead map of where you are, zoomable, with the real-time rain on it, drawn as
letters in a terminal. Built on [vtscreen](https://github.com/briefnotion/vtscreen).

**Status:** just started. Right now it opens the display, follows the window being
resized and shows an empty picture. Planned, in this order: one radar frame at a fixed
place, zoom and pan, a base map, an animated loop of the last hour or two, alerts.

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
place. `settings.json` is in `.gitignore`, so your location is never committed.

## Third-party code

- [vtscreen](https://github.com/briefnotion/vtscreen) (MIT), the drawing library, in `vtscreen/`.
- [nlohmann/json](https://github.com/nlohmann/json) (MIT), in `third_party/json/`, pinned to
  release v3.12.0.

## Licence

MIT, see `LICENSE`.
