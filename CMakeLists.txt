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

add_executable(vtweather src/main.cpp)
# the root is an include path, so sources say #include "vtscreen/vtscreen.h"
target_include_directories(vtweather PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vtweather PRIVATE nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vtweather PRIVATE -Wall -Wextra)
endif()
