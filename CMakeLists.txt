cmake_minimum_required(VERSION 3.16)
project(vtweather LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The libraries are git submodules: vtscreen/, vtpen/ (the pen that draws the map's lines) and third_party/json/
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vtscreen/vtscreen.h" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vtpen/include/vtpen.h" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/CMakeLists.txt")
    message(FATAL_ERROR "A library is missing. The libraries are git submodules; run:\n"
                        "    git submodule update --init")
endif()
# vtpen is headers only; vtscreen includes them (when it finds them) to draw shapes as ReGIS lines or Sixel pixels
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/vtpen/include")
add_subdirectory(third_party/json)          # nlohmann_json::nlohmann_json (settings, API replies)
find_package(CURL)                          # fetching from the web (libcurl, and its development files)
if(NOT CURL_FOUND)
    message(FATAL_ERROR "libcurl's development files are missing. On Debian, Ubuntu and Raspberry Pi OS:\n"
                        "    sudo apt install libcurl4-openssl-dev")
endif()
find_package(Threads REQUIRED)

# Strict: every warning is an error, for the program and the tests alike. This is how rasfled2 is built.
# -DVTWEATHER_STRICT=OFF keeps the warnings but lets the build finish (a newer compiler may warn about
# something an older one did not).
option(VTWEATHER_STRICT "Treat every compiler warning as an error" ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
    if(VTWEATHER_STRICT)
        add_compile_options(-Werror)
    endif()
endif()

set(VTWEATHER_SOURCES src/settings.cpp src/view.cpp src/draw_grid.cpp src/request_policy.cpp src/fetch.cpp src/inflate.cpp src/png.cpp src/radar_frame.cpp src/radar_source.cpp src/radar_worker.cpp
    src/radar_scheduler.cpp src/radar_cache.cpp src/store.cpp src/radar_feed.cpp src/cell_grid.cpp src/draw_radar.cpp src/input.cpp src/basemap.cpp src/ground_map.cpp src/draw_ground.cpp
    src/place_labels.cpp src/draw_places.cpp src/keys.cpp src/prompt.cpp src/layers.cpp src/card.cpp src/cards.cpp src/gps_feed.cpp src/car.cpp src/orientation.cpp src/pen_map.cpp src/pen_support.cpp src/rain_contours.cpp src/rain_pen.cpp src/rain_style.cpp)

set(VTWEATHER_SOURCES_NO_MAIN ${VTWEATHER_SOURCES})
add_executable(vtweather src/main.cpp ${VTWEATHER_SOURCES})
# the root is an include path, so sources say #include "vtscreen/vtscreen.h"
target_include_directories(vtweather PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# where to look for data/basemap.bin when it is not in the folder you run from
target_compile_definitions(vtweather PRIVATE VTW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather PRIVATE nlohmann_json::nlohmann_json CURL::libcurl Threads::Threads)

# a pretend GPS for trying vtweather without a car (see README: "Trying it without a car")
add_executable(gps_sim tools/gps_sim.cpp src/settings.cpp src/view.cpp)
target_include_directories(gps_sim PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_link_libraries(gps_sim PRIVATE nlohmann_json::nlohmann_json Threads::Threads)

# tests: cmake --build build && ctest --test-dir build --output-on-failure
# Only wanted when developing: -DVTWEATHER_TESTS=OFF skips the test programs (and the map
# maker), which makes the build a good deal shorter, for example on a Raspberry Pi.
option(VTWEATHER_TESTS "Build the test programs and the map maker" ON)
if(VTWEATHER_TESTS)
enable_testing()
add_executable(vtweather_tests tests/tests.cpp src/settings.cpp src/view.cpp src/request_policy.cpp src/radar_scheduler.cpp src/radar_cache.cpp src/radar_frame.cpp src/png.cpp src/inflate.cpp src/input.cpp src/keys.cpp src/prompt.cpp src/basemap.cpp)
target_include_directories(vtweather_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_tests COMMAND vtweather_tests)

# the fetch tests use a small server on this machine: nothing goes to the internet
add_executable(vtweather_fetch_tests tests/fetch_tests.cpp src/fetch.cpp src/request_policy.cpp)
target_include_directories(vtweather_fetch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_fetch_tests PRIVATE CURL::libcurl Threads::Threads)
add_test(NAME vtweather_fetch_tests COMMAND vtweather_fetch_tests)

# the PNG reader: every picture in tests/data must decode to the pixels the generator expects,
# and odd or damaged files must be refused (see tests/make_png_fixtures.py)
add_executable(vtweather_png_tests tests/png_tests.cpp src/png.cpp src/inflate.cpp)
target_include_directories(vtweather_png_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_png_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
add_test(NAME vtweather_png_tests COMMAND vtweather_png_tests)

# colours to strengths: the scale, and looking a frame up by place
add_executable(vtweather_radar_frame_tests tests/radar_frame_tests.cpp src/radar_frame.cpp src/png.cpp src/inflate.cpp)
target_include_directories(vtweather_radar_frame_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_radar_frame_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
add_test(NAME vtweather_radar_frame_tests COMMAND vtweather_radar_frame_tests)

# the picture address, and the worker thread that fetches and reads pictures
add_executable(vtweather_worker_tests tests/worker_tests.cpp src/radar_source.cpp src/radar_worker.cpp
               src/radar_frame.cpp src/png.cpp src/inflate.cpp src/fetch.cpp src/request_policy.cpp)
target_include_directories(vtweather_worker_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_worker_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(vtweather_worker_tests PRIVATE CURL::libcurl Threads::Threads)
add_test(NAME vtweather_worker_tests COMMAND vtweather_worker_tests)

# the cell grid and drawing the rain
add_executable(vtweather_draw_tests tests/draw_tests.cpp src/cell_grid.cpp src/draw_radar.cpp src/view.cpp
               src/radar_frame.cpp src/png.cpp src/inflate.cpp src/draw_ground.cpp src/ground_map.cpp src/basemap.cpp src/layers.cpp)
target_include_directories(vtweather_draw_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME vtweather_draw_tests COMMAND vtweather_draw_tests)

# the radar feed: worker and scheduler together
add_executable(vtweather_feed_tests tests/feed_tests.cpp ${VTWEATHER_SOURCES_NO_MAIN})
target_include_directories(vtweather_feed_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_feed_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(vtweather_feed_tests PRIVATE nlohmann_json::nlohmann_json CURL::libcurl Threads::Threads)
add_test(NAME vtweather_feed_tests COMMAND vtweather_feed_tests)

# the map maker: turns Natural Earth's GeoJSON into data/basemap.bin (see tools/get_natural_earth.sh)
add_executable(make_basemap tools/make_basemap.cpp src/basemap.cpp)
target_include_directories(make_basemap PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(make_basemap PRIVATE nlohmann_json::nlohmann_json)

# the base map: the file, the simplifier, filling cells, and known places on the real data
add_executable(vtweather_ground_tests tests/ground_tests.cpp src/basemap.cpp src/ground_map.cpp src/view.cpp)
target_include_directories(vtweather_ground_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_ground_tests PRIVATE VTW_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME vtweather_ground_tests COMMAND vtweather_ground_tests)

# the pen map: the map's lines as pen lines, kept clear of text, and what the terminal's answers lead to
add_executable(vtweather_pen_tests tests/pen_tests.cpp src/pen_map.cpp src/pen_support.cpp src/draw_ground.cpp src/ground_map.cpp src/layers.cpp src/basemap.cpp src/view.cpp src/input.cpp src/keys.cpp src/prompt.cpp src/settings.cpp src/rain_contours.cpp src/rain_pen.cpp src/rain_style.cpp src/radar_frame.cpp src/png.cpp src/inflate.cpp src/cell_grid.cpp)
target_include_directories(vtweather_pen_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_pen_tests PRIVATE VTW_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" VTW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_pen_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_pen_tests COMMAND vtweather_pen_tests)

# the rain as contour lines: the bands, following a level through a lattice, line styles, the pen drawing, tints, the key
add_executable(vtweather_rain_tests tests/rain_tests.cpp src/pen_map.cpp src/pen_support.cpp src/draw_ground.cpp src/ground_map.cpp src/layers.cpp src/basemap.cpp src/view.cpp src/input.cpp src/keys.cpp src/prompt.cpp src/settings.cpp src/rain_contours.cpp src/rain_pen.cpp src/rain_style.cpp src/radar_frame.cpp src/png.cpp src/inflate.cpp src/cell_grid.cpp
               src/card.cpp src/cards.cpp src/draw_radar.cpp src/car.cpp src/gps_feed.cpp)
target_include_directories(vtweather_rain_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_rain_tests PRIVATE VTW_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data" VTW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_rain_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_rain_tests COMMAND vtweather_rain_tests)

# city names: which to show, and where
add_executable(vtweather_places_tests tests/places_tests.cpp src/place_labels.cpp src/basemap.cpp src/view.cpp)
target_include_directories(vtweather_places_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_places_tests PRIVATE VTW_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME vtweather_places_tests COMMAND vtweather_places_tests)

# what is kept on disk: radar pictures and the remembered view
add_executable(vtweather_store_tests tests/store_tests.cpp src/store.cpp)
target_include_directories(vtweather_store_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_store_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_store_tests COMMAND vtweather_store_tests)

# reading keys and the go-to prompt
add_executable(vtweather_prompt_tests tests/prompt_tests.cpp src/keys.cpp src/prompt.cpp src/basemap.cpp src/view.cpp)
target_include_directories(vtweather_prompt_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_prompt_tests PRIVATE VTW_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME vtweather_prompt_tests COMMAND vtweather_prompt_tests)

# the part of the vtscreen library vtweather leans on: how a cell's shimmer picks its letter
add_executable(vtweather_library_tests tests/library_tests.cpp)
target_include_directories(vtweather_library_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME vtweather_library_tests COMMAND vtweather_library_tests)

# the cards: the frame, wrapping, corners, stacking, what each says and when they come and go
add_executable(vtweather_card_tests tests/card_tests.cpp src/card.cpp src/cards.cpp src/view.cpp src/draw_radar.cpp src/cell_grid.cpp src/rain_style.cpp
               src/radar_frame.cpp src/png.cpp src/inflate.cpp src/layers.cpp src/car.cpp src/gps_feed.cpp)
target_include_directories(vtweather_card_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_card_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_card_tests COMMAND vtweather_card_tests)

# the GPS reader: the file's layout, how fresh it is, damaged files, and the real writer's output
add_executable(vtweather_gps_tests tests/gps_tests.cpp src/gps_feed.cpp)
target_include_directories(vtweather_gps_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(vtweather_gps_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(vtweather_gps_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_gps_tests COMMAND vtweather_gps_tests)

# the car: moving or not, holding the position, the arrow, drawing it
add_executable(vtweather_car_tests tests/car_tests.cpp src/car.cpp src/gps_feed.cpp src/view.cpp)
target_include_directories(vtweather_car_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_car_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_car_tests COMMAND vtweather_car_tests)

# the pretend GPS's drive, and the file it writes, read back by the reader and the car
add_executable(vtweather_gps_sim_tests tests/gps_sim_tests.cpp src/car.cpp src/gps_feed.cpp src/view.cpp)
target_include_directories(vtweather_gps_sim_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_compile_definitions(vtweather_gps_sim_tests PRIVATE VTW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(vtweather_gps_sim_tests PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
add_test(NAME vtweather_gps_sim_tests COMMAND vtweather_gps_sim_tests)

# which way is up: when the map turns, easing round, and the layers drawn with the map turned
add_executable(vtweather_orientation_tests tests/orientation_tests.cpp src/orientation.cpp src/car.cpp src/gps_feed.cpp src/view.cpp
               src/cell_grid.cpp src/draw_grid.cpp src/draw_radar.cpp src/layers.cpp src/radar_frame.cpp src/png.cpp src/inflate.cpp)
target_include_directories(vtweather_orientation_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather_orientation_tests PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME vtweather_orientation_tests COMMAND vtweather_orientation_tests)
endif()  # VTWEATHER_TESTS
