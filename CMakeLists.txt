cmake_minimum_required(VERSION 3.16)
project(vtweather LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The two libraries are git submodules: vtscreen/ and third_party/json/
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vtscreen/vtscreen.h" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/CMakeLists.txt")
    message(FATAL_ERROR "A library is missing. The libraries are git submodules; run:\n"
                        "    git submodule update --init")
endif()
add_subdirectory(third_party/json)          # nlohmann_json::nlohmann_json (settings, API replies)
find_package(CURL REQUIRED)                 # fetching from the web (libcurl, and its development files)
find_package(Threads REQUIRED)

set(VTWEATHER_SOURCES src/settings.cpp src/view.cpp src/draw_grid.cpp src/request_policy.cpp src/fetch.cpp src/inflate.cpp src/png.cpp src/radar_frame.cpp)

add_executable(vtweather src/main.cpp ${VTWEATHER_SOURCES})
# the root is an include path, so sources say #include "vtscreen/vtscreen.h"
target_include_directories(vtweather PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather PRIVATE nlohmann_json::nlohmann_json CURL::libcurl Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vtweather PRIVATE -Wall -Wextra)
endif()

# tests: cmake --build build && ctest --test-dir build --output-on-failure
enable_testing()
add_executable(vtweather_tests tests/tests.cpp src/settings.cpp src/view.cpp src/request_policy.cpp)
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
