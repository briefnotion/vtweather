# vtweather

An overhead map of where you are, zoomable, with the real-time rain on it, drawn as
letters in a terminal. Built on [vtscreen](https://github.com/briefnotion/vtscreen).

**Status:** it works end to end, in a first form. It reads its settings, draws the land and water
(a base map made from Natural Earth data, with rivers, borders, major roads and city names), asks NOAA's radar service for a picture of the area on
screen (in the background, so the screen never waits), turns the colours into rain strengths and
draws the rain as letters on top. You can zoom and move around with the keys. It follows the window
being resized, and fetches the radar again every few minutes and when the view moves off the
picture. Not yet: the animated loop of the last hour or two, alerts.

## Build and run

Linux, `g++` with C++17, `cmake` 3.16 or later, and the libcurl development files
(`libcurl4-openssl-dev` on Debian and Ubuntu). The libraries are git submodules, so
clone with `--recursive`:

    git clone --recursive https://github.com/briefnotion/vtweather.git
    cd vtweather
    cmake -B build
    cmake --build build
    ./build/vtweather             (q quits; also --settings FILE, --offline, --no-cache, --no-color,
                                   --fps N, --cols N --rows N)

If `vtscreen/` or `third_party/json/` is empty, run `git submodule update --init`.

## Settings

`settings.sample.json` shows the settings. Copy it to `settings.json` and put in your own
place. `settings.json` is in `.gitignore`, so your location is never committed. The program
reads `settings.json` from the folder you run it in, else `settings.sample.json`, else uses
built-in defaults; `--settings FILE` names another file. A missing or damaged file never stops
it: the notes on screen say what was wrong and what was used instead.

| setting | meaning |
|---|---|
| `location` | `name`, `lat`, `lon`: where the map is centred, in degrees (north and east positive; the latitude is limited to 80 either way) |
| `zoom_km` | the ground distance across the screen, 2 to 6000 km |
| `cell_aspect` | how many times taller than wide a terminal cell is, 1 to 3 (2.0 suits most) |
| `refresh_minutes` | how often to ask for new radar, never less than 5: the radar updates about that often and the data comes from a free service |
| `user_agent_contact` | optional, how a data service can reach you; sent in the User-Agent |
| `remember_view` | `true` (the default): start where you were looking last time; `r` still goes to `location` |
| `cache_dir` | where pictures and the last view are kept; empty means the usual place (see below) |

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
- the program itself asks for a picture (two small requests, the scan time and the picture) only at start, then when the picture in use is older than
  `refresh_minutes` (never under 5), and when the view has moved off every picture it holds and
  stayed there for a second; never twice within 5 seconds, and after a failure it waits at least 10
  seconds, or as long as the service asked;
- it keeps the last 8 pictures in memory, so going back to a place you looked at a moment ago, or
  zooming out and in again, downloads nothing: the newest picture that covers the view is used (a
  new picture replaces older ones it covers, so nothing is kept twice);
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
| `w` `a` `s` `d`, or the arrow keys | move the view up (north), left, down, right, by a tenth of the screen; hold a key to keep moving |
| `/` or `l` | go to a city: type part of its name, the matches are listed (the biggest first), Up and Down choose one, Enter goes there and Escape gives up |
| `r` | back to the place and zoom in your settings |
| `g` | show or hide a grid of latitude and longitude lines |
| `n` | show or hide the city names |
| `k` | show or hide the key to the rain (a strip from light to heavy in the lower right) |
| `q` | quit |

Moving or zooming out onto an area the radar picture does not cover makes the program ask for a new
one, but only after you have stayed there for a second, so holding a key does not send a request
for every press. Zooming in never needs a new picture: the radar's own detail is about 1 km.
`--offline` asks the radar service for nothing at all, which is handy for trying the keys.

## Going to a place by name

Press `/` (or `l`) and type part of a city's name, such as `wich` or `portland`. The cities that
match are listed above the line you are typing (those that start with what you typed first, the
biggest first), with their latitude and longitude, because several places can share a name. Up and
Down choose one, Enter moves the view there (zooming in to 150 km across if you were further out
than 400 km) and Escape closes the prompt. While it is open a `q` is a letter like any other, so the
program does not quit. The names come from the base map's own list of about 2300 cities, so this
works offline and asks nobody anything. Arrow keys work through the escape sequences a Linux
terminal sends; they are not read on Windows yet.

## Kept between runs

The program keeps two things on disk, in a folder of its own, `~/.cache/vtweather` (or
`$XDG_CACHE_HOME/vtweather`; the `cache_dir` setting names another), never in the repository:

- the last 4 radar pictures, so a restart shows rain at once (and, if a picture is older than the
  refresh time, it is shown marked "(old)" while a new one is asked for), and with no network, or
  with `--offline`, the last saved picture is still there with its age. Pictures older than 6 hours
  are not loaded;
- where you were looking: the centre, the zoom, and whether the grid and the names were on. It is
  saved every 30 seconds and when you quit, and loaded when you start (unless `remember_view` is
  `false`). `r` goes back to the place in your settings.

Both files tell a little about where you live, which is why they are kept out of the repository.
`--no-cache` turns all of this off for one run: nothing is read or written. A missing or damaged
file is never an error; the program just starts without it.

## The base map

The ground is drawn from `data/basemap.bin` (4 MB), a compact file of land and lake polygons, of
country borders, state and province borders, rivers and roads as lines, and of about 2300 cities, which the program reads itself
(`src/basemap.cpp`). It was made from [Natural Earth](https://www.naturalearthdata.com) (public
domain) at three levels of detail, about 1:110 million for views of thousands of kilometres,
1:50 million for hundreds and 1:10 million for close views; the finest level covers North America
only, since that is all the radar covers, and elsewhere a close view uses the next level.

Water is a flat navy (8, 22, 48): its letters are the same navy as its background, so in colour you
see a plain navy block, and without colour you see faint dots that hardly move (the shimmer is set
as low as it can be without stopping, one change of letter in about 80 seconds). Land has no colour
and no letters at all: it shows your terminal's own background, which is empty without colour.
Rivers, roads, state borders, country borders and the coast are lines of letters on the land, each a
step brighter than the last (blue, amber, violet, pink, tan) and all kept dim so the rain stands out;
they have no background colour of their own. The ground of a view is worked out again only when the
view changes. The file is found in `data/` of the folder you run from, or `../data/`, or the source
tree it was built from; `--basemap FILE` names another. Without one the program still runs, with no
ground.

Roads are the major highways (Interstates and the like, brighter amber) and the other highways
(darker amber) of North America, from Natural Earth's 10 m roads. They are shown only when you are
close enough for them to matter: major highways below 1000 km across the screen and the other
highways below 250 km. There are no local streets.

City names (`n` hides them) are a dot and the name beside it, as text so they are always readable,
white for big cities and grey for small ones. A view of thousands of kilometres shows only the
biggest cities (3 million and up), and each step closer adds smaller ones, down to towns of 15 000
or less when you are close. The biggest are placed first, and a name that would run into one
already placed, or off the screen, is left out, so names never overlap; if there is no room on the
right of the dot the name goes on the left. Names are plain ASCII (Natural Earth's ASCII names). The file is found in `data/` of the folder you run from, or `../data/`, or the source
tree it was built from; `--basemap FILE` names another. Without one the program still runs, with no
ground.

To remake the file (only needed if the map data changes):

    ./tools/get_natural_earth.sh                          downloads the GeoJSON (about 125 MB, the roads being 50 of it), once, politely
    ./build/make_basemap tools/natural_earth data/basemap.bin

## Where things overlap

Rivers, roads, borders, the coast, the grid and the rain are layers. A cell with one of them holds its
letter still. Where layers overlap, the cell shimmers, by a base rate for each layer beyond the first:
two layers shimmer at the base rate, three at twice it, four at three times it. The shimmer works
without colour too, which is how an overlap shows there. How the cell looks (its brightness and
colour) is the top layer's, not a mix, in this order of priority, highest first: rain, the grid,
borders, the coast, roads, rivers. The base is `OVERLAP_SHIMMER` in `src/layers.h` (0.875: two layers
change letter about ten times a second). The two kinds of border (state and country) count as one
layer, and so do the two kinds of road; the land and the water are the ground the layers sit on and
are not layers. The order the layers are drawn in makes no difference.

A cell with one layer has a shimmer of 0, and the vtscreen library shows a shimmer of 0 as the first
letter of the cell's brightness group, the same in every such cell, so a river, a road or a border is
a clean line of one letter and an overlap, whose letters vary, stands out against it. Without colour
the kinds therefore differ by that letter: `:` for the grid, rivers and the other roads, `=` for
state borders and highways, `i` for country borders, `o` for the coast and the faintest rain, and
heavier rain through `0` and `#` up to solid blocks. (Rivers, other roads and the grid share a
brightness step, so they share a letter; so do state borders and highways. Colour tells them apart.)

## Seeing the rain

The program draws the area in `settings.json` (the sample is Wichita, Kansas). To look at weather
elsewhere, change `location` and `zoom_km` there. Stronger rain is brighter (so it also reads with
`--no-color`); in colour it follows the weather scale: pale blue, green, yellow,
orange, red, magenta. The line at the top left says how the radar is doing. Normally it gives the time of the radar's
own scan ("radar: scan 05:18 (11:18Z), 1 min old": local time, then UTC), and marks the picture
"(old)" when it is older than the refresh time; otherwise it says it is asking, or why an update failed
and when it will try again.

## Where the radar covers

NOAA's radar covers the continental United States and a margin, 20 to 55 degrees north and 130 to 60
degrees west. The program asks for a picture of only the part of the view inside that area (so a
picture is never labelled with more than it shows), and nothing at all when the view is wholly outside
it; the line at the top then says "radar: none here". The view cannot be centred nearer than 80
degrees to a pole: the map is the usual web map (Mercator), which stretches without limit toward the
poles, so that near them a screen wider than the earth would show the world repeated.

## The radar's scan time

Just before each picture the program asks NOAA's map service for its capabilities, a small document
(about 18 KB) that lists the radar layer's scan times; the newest is its default. The program then
asks for the picture of exactly that scan, so the time shown always belongs to the picture. The
radar scans about every two minutes, and the time is kept with the picture on disk. If the document
cannot be read the newest picture is asked for anyway, and the line says when it was fetched
instead. This doubles the number of requests, but the extra one is small and obeys the same rules.

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
PNG reader, the colour scale, the worker thread, the picture cache, what is kept on disk, the keys and the base map (its file format, the
ring simplifier, filling land and water into cells, and known places on the real file). The fetch tests talk to a small server on your own machine; nothing goes to the
internet.

`tests/data/` holds the PNG test pictures, made by `tests/make_png_fixtures.py` (it needs Pillow,
which also checks the expected pixels, and it is only needed to remake them). Most are synthetic:
every colour type and bit depth, with transparency, every row filter and several compression
styles. Two are real answers from NOAA's radar map service (US government data, public domain).
The test also feeds the reader thousands of shortened and randomly damaged files; running it
under ASan and UBSan is a good idea after any change to the reader. The worker and fetch tests
can also be run under ThreadSanitizer (`-fsanitize=thread`; if it stops with "unexpected memory
mapping", start the program with `setarch x86_64 -R`).

## Third-party code and data

- [Natural Earth](https://www.naturalearthdata.com) (public domain): the land, lake, border, river, road and city data in `data/basemap.bin`. Natural
  Earth asks for no credit; "Made with Natural Earth" is its suggested line.
- [vtscreen](https://github.com/briefnotion/vtscreen) (MIT), the drawing library, in `vtscreen/`.
- [nlohmann/json](https://github.com/nlohmann/json) (MIT), in `third_party/json/`, pinned to
  release v3.12.0.

## Licence

GNU General Public License v3.0, see `LICENSE`. The libraries it uses, vtscreen and nlohmann/json,
are MIT-licensed and keep their own licences in their folders.
