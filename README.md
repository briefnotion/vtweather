# vtweather

An overhead map of where you are, zoomable, with the real-time rain on it, drawn as
letters in a terminal. Built on [vtscreen](https://github.com/briefnotion/vtscreen).

**Status:** it works end to end, in a first form. It reads its settings, draws the land and water
(a base map made from Natural Earth data, with rivers, borders, major roads and city names), asks NOAA's radar service for a picture of the area on
screen (in the background, so the screen never waits), turns the colours into rain strengths and
draws the rain as letters on top. You can zoom and move around with the keys. It follows the window
being resized, and fetches the radar again every few minutes and when the view moves off the
picture. Not yet: the animated loop of the last hour or two, alerts.

## Quick start (Raspberry Pi OS, Debian, Ubuntu)

Three steps, each a block you can paste into a terminal as it is.

1. The tools it needs (once):

        sudo apt install -y git g++ cmake libcurl4-openssl-dev

2. Get the code. The extra options keep the download small by leaving out the history of
   the JSON library it uses:

        git clone --recursive --shallow-submodules --depth 1 https://github.com/briefnotion/vtweather.git

3. Build it and run it (a few minutes on a Raspberry Pi; `-j4` uses all four cores and
   `-DVTWEATHER_TESTS=OFF` skips the test programs, which you do not need to run it):

        cd vtweather && cmake -B build -DVTWEATHER_TESTS=OFF && cmake --build build -j4 && ./build/vtweather

It needs an internet connection for the radar. Without one it still draws the map.

- **First run:** press `/` and type the name of your city, then Enter. The map and the
  zoom you leave it on are remembered for next time. `+` and `-` zoom, `w a s d` or the
  arrow keys move, `q` quits (all the keys are under "Keys" below).
- **Run it again later:** `cd ~/vtweather && ./build/vtweather`
- **Your home, and a contact for the data service:** `cp settings.sample.json settings.json`
  and edit it (see "Settings"). `settings.json` is never committed.
- **The plain Linux text console** (no desktop) has no colours: add `--no-color`. The
  letters then tell the story on their own.
- **A small screen or a slow Pi:** make the terminal window smaller, or add `--fps 15`.
- **Update to the newest version:**

        cd ~/vtweather && git pull && git submodule update --init && cmake --build build -j4

- **Nothing happens, or it says a library is missing:** `git submodule update --init` fetches
  the two libraries if the clone missed them, and the apt line above installs the rest.

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
`-DVTWEATHER_TESTS=OFF` on the first `cmake` line leaves out the test programs and the map
maker (see "Tests").

The build is strict: every compiler warning is an error (`-Wall -Wextra -Werror`), for the program
and the tests, as in rasfled2. It is clean with g++ 12 and 13. If a newer compiler warns about
something and stops the build, `-DVTWEATHER_STRICT=OFF` on the first `cmake` line shows the warning
and finishes anyway (and please report it).

## Settings

`settings.sample.json` shows the settings. Copy it to `settings.json` and put in your own
place. `settings.json` is in `.gitignore`, so your location is never committed. The program
reads `settings.json` from the folder you run it in, else `settings.sample.json`, else uses
built-in defaults; `--settings FILE` names another file. A missing or damaged file never stops
it: the notes card on screen says what was wrong and what was used instead (see "The cards").

| setting | meaning |
|---|---|
| `location` | `name`, `lat`, `lon`: where the map is centred, in degrees (north and east positive; the latitude is limited to 80 either way) |
| `zoom_km` | the ground distance across the screen, 2 to 6000 km |
| `cell_aspect` | how many times taller than wide a terminal cell is, 1 to 3 (2.0 suits most) |
| `refresh_minutes` | how often to ask for new radar, never less than 5: the radar updates about that often and the data comes from a free service |
| `user_agent_contact` | optional, how a data service can reach you; sent in the User-Agent |
| `remember_view` | `true` (the default): start where you were looking last time; `r` still goes to `location` |
| `cache_dir` | where pictures and the last view are kept; empty means the usual place (see below) |
| `gps_file` | the file the GPS position is read from (see "The GPS"); `/dev/shm/exchange/gps.json` by default, empty means no GPS |
| `moving_mph` | the speed at and above which the car counts as moving, 1 to 30 (5 by default; see "The car on the map") |

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
| `f` | follow the car: the map stays centred on it (needs the GPS, see "The GPS"). Any move of your own, the move keys, `r` or going to a city, stops following; `f` again starts it and the map jumps back to the car. Zooming keeps following. It starts off, and is not remembered between runs |
| `h` | heading up: turn the map so the way the car is going is up the screen, like a car's navigation screen. It turns only while the map follows the car (`f`) and the car is moving; when you slow down or stop, it turns back to north up by itself, and again when you move off. North up is the default, and `h` is not remembered between runs |
| `r` | back to the place and zoom in your settings |
| `g` | show or hide a grid of latitude and longitude lines |
| `n` | show or hide the city names |
| `k` | show or hide the rain card, the key to the rain (a strip from light to heavy, in the lower right) |
| `q` | quit |

Any key press also brings the keys card back for 30 seconds (see "The cards").

Moving or zooming out onto an area the radar picture does not cover makes the program ask for a new
one, but only after you have stayed there for a second, so holding a key does not send a request
for every press. Zooming in never needs a new picture: the radar's own detail is about 1 km.
`--offline` asks the radar service for nothing at all, which is handy for trying the keys.

## The cards

The information is in small framed windows, cards, drawn over the map, with the title set into the
top edge of a thin rounded border (like the panels of `btop`). They have a dim background so the text
reads over the map. Without colour the frames and the text are still there.

| card | where | shows |
|---|---|---|
| view | top right, always | titled with the place name from your settings; the centre of the map, how far across it is and how far each column is ("572 km across, 4.93 km per column"), how the radar is doing (the scan time, how old, or why an update failed), and which way is up ("north up", or where north is when the map is turned) |
| notes | under the view card | warnings from starting up, in amber (a settings file that was not found, a missing base map): until your first key press |
| keys | under that | the keys, one to a row: at the start, and for 30 seconds after any key press, so it is there while you are zooming and moving and goes away when you are not; not while the go-to prompt is open |
| driving | bottom left, when there is a GPS file | (titled "driving, following" while the map follows the car) what the GPS says, in miles per hour and feet: the position, the speed (and the heading while moving), the height and the satellites; amber for "no fix yet", "lost, nothing for 13 s" and "unreadable" |
| rain | bottom right | the key to the rain: a strip from light to heavy rain in the same letters and colours as the map (`k` shows and hides it); only when a radar picture is on screen and the screen is at least 70 x 16 |

Long lines wrap (a card is at most 44 characters of text wide). A card that does not fit under the
ones above it is left out, the notes before the keys, and the stack stops short of the rain card.
Cards are plain data (`Card`: a title, lines, and an optional strip of colour cells), drawn by
`draw_card` in `src/card.cpp`; what goes on each one and how they are stacked is in `src/cards.cpp`.

## The GPS

On the Raspberry Pi in the car, the position comes from rasfled2, which already reads the GPS. It writes
the position about once a second to a small JSON file in memory, `/dev/shm/exchange/gps.json` (a folder in
RAM, so nothing wears out the SD card), and vtweather only reads that file. The two programs know nothing
else about each other: if rasfled2 is not running there is simply no GPS. Change `gps_file` in your settings
to read it from somewhere else; an empty `gps_file` turns it off.

    { "version": 1, "seq": 1234, "written_unix": 1764079402.123, "gps_time_unix": 1764079402.0,
      "fix": true, "lat": 37.690, "lon": -97.340, "altitude_m": 412.0, "speed_kmph": 88.6,
      "heading_deg": 87.0, "heading_valid": true, "satellites": 9, "hdop": 1.2 }

The writer is `source/gps_exchange.h` in rasfled2 (`gps_exchange_write`, called for each `$GNGGA`). It
writes a new file and renames it over the old one, so a reader never sees half a file. The reader is
`GpsFeed` in `src/gps_feed.cpp`: about four times a second it looks at the file's modification time, and
only when that has changed does it read and parse the file. A damaged file is refused with a reason, and the last
good reading is kept. How fresh the data is comes from the file's age against the clock (both are the same
computer's): the states are

| state | meaning |
|---|---|
| off | `gps_file` is empty |
| no file | there is no file: rasfled2 is not running, or has not told us anything yet (nothing is shown) |
| unreadable | the newest file could not be read; the reason is shown |
| lost | the file has not been written for 5 seconds (rasfled2 stopped, or the GPS was unplugged); a file left over from long ago is lost at once |
| no fix | the GPS is working but has no position yet |
| fix | a good position, fresh |

What it says is on the driving card (bottom left, see "The cards"), and the car is on the map (next section).
Following the car with the map, and a heading-up map, come next. The reader is Linux only for now (the file's
time comes from `stat()`).

## The car on the map

A GPS is not steady when it stands still: the position wanders by tens of metres, and the direction it reports
is noise. So the car has two looks, decided by its speed (`moving_mph` in the settings, 5 by default):

- at that speed and above it is moving: a white arrow (`↑ ↗ → ↘ ↓ ↙ ← ↖`, in eight steps) shows the direction of
  travel at the car's live position;
- below it, a plain white `+` is drawn, and the position is held where it was when the car was last moving, so a
  car stopped at a light stays put (and a map that follows the car will not crawl about). It moves again when the
  car does.

There is a gap so that it does not flicker around the limit: it turns on at 5 mph and off again only below 3.
The home marker stays a yellow `+`. The marker is only drawn while there is a fix, and the first fix, or a fix that
comes back after being lost, always starts from the live position (a held position from long ago is no use). The
driving card still shows the live speed and position, drift and all.

Press `f` to follow the car: the map is kept centred on the marker (so a held position does not crawl either), and the
driving card's title says "driving, following". Any move of your own stops it (the move keys, `r`, going to a city),
and `f` starts it again from wherever the car is. Zooming keeps following. While there is no fix the map stays where it
is, and carries on following when the fix is back. Following starts off each time the program starts.

Press `h` for heading up: the map is turned so that the car's direction of travel is up the screen. It does this only
while the map follows the car (`f`) and the car counts as moving with a direction (the arrow is showing); when you
slow below the limit, stop, lose the fix or move the map yourself, it turns back to north up, and it turns again when
you move off. The turn is smooth: the GPS gives a new direction once a second, and the map eases round to it (at most
120 degrees a second, the short way round) instead of jumping. A GPS wobbles a degree or so, and every turn redraws
the whole map, so a map that has settled ignores a direction less than 2 degrees away, and only goes all the way to
the new direction once it has started to turn. Names and the cards stay upright. The last line of the view card says
which way is up: "north up", "heading up, north ↗" (where north has gone), or "heading up (north up while slow)".
Each frame the map is turning costs about ten times what a still one does (around 4 ms for a 160 x 49 screen on a
desktop computer), and only while it is turning.

## Trying it without a car

`gps_sim` is a pretend GPS: it drives a car round a block, over and over, and writes the same file rasfled2 writes
(see "The GPS"), once a second, so you can watch vtweather's driving card and car marker at your desk. It is built
with the program (`build/gps_sim`). Run it in one terminal and vtweather in another:

    ./build/gps_sim                          start where settings.json says home is
    ./build/vtweather

The drive repeats every 165 seconds, after the first seconds with no fix: pulling away, cruising at 50 mph, a right
turn, a light (braking, then standing still for 12 seconds, where the position wanders by a few metres, the speed
jitters and the heading is noise), another corner, 55 mph, 8 seconds in a tunnel (nothing is written, so vtweather
says "lost" after 5), a creep at 4 mph, and round again. Each line it prints says which part of the drive it is in
and what it just wrote. Ctrl-C quits and removes the file.

Options: `--lat D --lon D` (start somewhere else), `--heading D` (0 is north, 90 east, the default), `--warp N` (cover
N times the distance, to cross a zoomed-out map; the speed it reports stays real), `--hz N` (writes a second, 1 by
default), `--no-tunnel`, `--file PATH` (default `/dev/shm/exchange/gps.json`, like `gps_file`), `--settings FILE`,
`--keep` (leave the file when it quits) and `--force`. It refuses to start if the file was written in the last few
seconds, since something else (rasfled2) is writing it; do not run the two together. The drive and the file's
writer are in `tools/gps_sim_route.h`; the tests (`gps_sim_tests`) read what it writes with the real reader and run
the whole drive through the car.

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
orange, red, magenta. The last line of the view card (top right) says how the radar is doing. Normally it gives the time of the radar's
own scan ("radar: scan 05:18 (11:18Z), 1 min old": local time, then UTC), and marks the picture
"(old)" when it is older than the refresh time; otherwise it says it is asking, or why an update failed
and when it will try again.

## Where the radar covers

NOAA's radar covers the continental United States and a margin, 20 to 55 degrees north and 130 to 60
degrees west. The program asks for a picture of only the part of the view inside that area (so a
picture is never labelled with more than it shows), and nothing at all when the view is wholly outside
it; the radar line of the view card then says "radar: none here". The view cannot be centred nearer than 80
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

The tests are built by default. Configure with `-DVTWEATHER_TESTS=OFF` to leave them out.

    cmake --build build
    ctest --test-dir build --output-on-failure

They cover the View (scale, cell shape, north up, rotation, round trips, pan, zoom), the settings
loader (good, missing, damaged, wrong type, out of range), the request policy (with a made-up
clock), the fetcher (pacing, backoff, the hourly limit, timeouts, size limit, cancelling), the
PNG reader, the colour scale, the worker thread, the picture cache, what is kept on disk, the keys and the base map (its file format, the
ring simplifier, filling land and water into cells, and known places on the real file). The GPS reader has tests too (the file's layout, with files made by rasfled2's real writer; damaged, cut-off and odd files; and every state, with a made-up clock and files whose times are set by hand). So do the car (when it counts as moving, holding the position, the arrow and where it is drawn) and the pretend GPS (its drive, the drift, the tunnel, and its file read back by the real reader). The cards have tests too: the frame and its size, wrapping, each corner, stacking, what each card says, the rain card's strip, and when the keys and notes cards come and go. The fetch tests talk to a small server on your own machine; nothing goes to the
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
