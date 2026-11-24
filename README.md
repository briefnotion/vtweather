# vtweather

An overhead map of where you are, zoomable, with the real-time rain on it, drawn as
letters in a terminal. Built on [vtscreen](https://github.com/briefnotion/vtscreen).

**Status:** it works end to end, in a first form. It reads its settings, asks NOAA's radar service
for a picture of the area on screen (in the background, so the screen never waits), turns the
colours into rain strengths and draws the rain as letters on top of a graticule and a marker that
stand in for the map. You can zoom and move around with the keys. It follows the window being
resized, and fetches again every few minutes and when the view moves off the picture. Not yet: a
base map (coastlines and so on), the animated loop of the last hour or two, alerts.

## Build and run

Linux, `g++` with C++17, `cmake` 3.16 or later, and the libcurl development files
(`libcurl4-openssl-dev` on Debian and Ubuntu). The libraries are git submodules, so
clone with `--recursive`:

    git clone --recursive https://github.com/briefnotion/vtweather.git
    cd vtweather
    cmake -B build
    cmake --build build
    ./build/vtweather             (q quits; also --settings FILE, --offline, --no-color, --fps N,
                                   --cols N --rows N)

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

## Being kind to the data services

The radar comes from free services that owe us nothing, and they may be having a bad day
themselves. All web access goes through one place, `Fetcher::get()` in `src/fetch.cpp`, and the
rules it follows are in `src/request_policy.cpp`:

- every request says who is asking: `vtweather/0.1 (+https://github.com/briefnotion/vtweather;
  your contact)`; the contact comes from `user_agent_contact` in your settings;
- at least 2 seconds between one request ending and the next starting, one request at a time;
- after a failure (no answer, a timeout, any HTTP error) it stops asking: 30 seconds, then 1, 2,
  4 minutes and so on up to 30 minutes, and longer if the server sends `Retry-After`; a success
  starts over, and while it waits nothing goes out at all;
- never more than 120 requests an hour, as a safety net against a bug that loops;
- the program itself asks for a picture only at start, then every `refresh_minutes` (never under 5),
  and when the view has moved off the picture and stayed there for a second; never twice within 5
  seconds, and after a failure it waits at least 10 seconds, or as long as the service asked;
- only https, a 10 second connect and 30 second total time limit, an 8 MB size limit, and a
  request can be cancelled at once when the program quits.

## The PNG reader

The radar comes as PNG pictures, and there is no PNG reader in C++, so this project has its own
(`src/png.cpp` and `src/inflate.cpp`, no libraries). It reads every kind of PNG except interlaced
ones, checks everything (the signature, every chunk's checksum, sizes, the compressed data's
checksum) and refuses anything odd with a reason, since the pictures come off the network. A
1500 x 1000 picture takes about 40 milliseconds.

## Keys

| key | does |
|---|---|
| `+` or `=` | zoom in (each press shows 80 % of the distance; the closest is 2 km across) |
| `-` or `_` | zoom out (the widest is 6000 km) |
| `w` `a` `s` `d` | move the view up (north), left, down, right, by a tenth of the screen; hold a key to keep moving |
| `r` | back to the place and zoom in your settings |
| `q` | quit |

Moving or zooming out onto an area the radar picture does not cover makes the program ask for a new
one, but only after you have stayed there for a second, so holding a key does not send a request
for every press. Zooming in never needs a new picture: the radar's own detail is about 1 km.
`--offline` asks the radar service for nothing at all, which is handy for trying the keys.

## Seeing the rain

The program draws the area in `settings.json` (the sample is Wichita, Kansas). To look at weather
elsewhere, change `location` and `zoom_km` there. Stronger rain is brighter (so it also reads with
`--no-color`) and shimmers faster; in colour it follows the weather scale: pale blue, green, yellow,
orange, red, magenta. The line at the top left says how the radar is doing: asking, fetched, or why
an update failed and when it will try again.

## The worker thread

Fetching and reading a picture can take a second or more, so it never happens on the thread that
draws. `RadarWorker` (`src/radar_worker.cpp`) has a thread of its own that fetches, reads the PNG
and turns it into a `RadarFrame`. The main loop calls `request()` to ask for a picture and, once
a frame, `take()` to see whether it has arrived; neither call ever waits.

The two threads share exactly one atomic variable, the state (idle, requested, working, ready,
stop). Whoever the state points at owns the request and the result, and handing over is a single
store, so there are no locks. Quitting is quick even in the middle of a download. Because the
worker's fetcher follows the politeness rules, a result can say that nothing was asked yet.

## Rain strength

The radar service draws rain in the usual weather-service colours. `src/radar_frame.cpp` holds
that colour scale as a list of known colours, each with an estimated reflectivity in dBZ, and
turns every pixel into a strength: 0 for no rain, about 5 for the faintest echoes, 20 to 30 for
light to moderate rain, 35 and up for heavy rain. The program then works with strengths, never
colours, so rain can be drawn in colour or in plain letters alike. A screen cell that covers many
pixels takes the strongest one, so a storm's core is never averaged away.

The values are estimates (the service publishes no table), good for telling light rain from
heavy, not for measuring. The colours up to dark green were taken from real pictures; the strong
end (yellow, orange, red and beyond) is the standard scale, and strong colours that are not on
it are placed by their hue. Known gap: the strongest end has not yet been compared with a real
picture of heavy rain.

## Tests

    cmake --build build
    ctest --test-dir build --output-on-failure

They cover the View (scale, cell shape, north up, rotation, round trips, pan, zoom), the settings
loader (good, missing, damaged, wrong type, out of range), the request policy (with a made-up
clock), the fetcher (pacing, backoff, the hourly limit, timeouts, size limit, cancelling), the
PNG reader, the colour scale and the worker thread. The fetch tests talk to a small server on your own machine; nothing goes to the
internet.

`tests/data/` holds the PNG test pictures, made by `tests/make_png_fixtures.py` (it needs Pillow,
which also checks the expected pixels, and it is only needed to remake them). Most are synthetic:
every colour type and bit depth, with transparency, every row filter and several compression
styles. Two are real answers from NOAA's radar map service (US government data, public domain).
The test also feeds the reader thousands of shortened and randomly damaged files; running it
under ASan and UBSan is a good idea after any change to the reader. The worker and fetch tests
can also be run under ThreadSanitizer (`-fsanitize=thread`; if it stops with "unexpected memory
mapping", start the program with `setarch x86_64 -R`).

## Third-party code

- [vtscreen](https://github.com/briefnotion/vtscreen) (MIT), the drawing library, in `vtscreen/`.
- [nlohmann/json](https://github.com/nlohmann/json) (MIT), in `third_party/json/`, pinned to
  release v3.12.0.

## Licence

GNU General Public License v3.0, see `LICENSE`. The libraries it uses, vtscreen and nlohmann/json,
are MIT-licensed and keep their own licences in their folders.
