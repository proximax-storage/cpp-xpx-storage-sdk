cmake_minimum_required(VERSION 3.2)
project(upload)

set(CMAKE_CXX_STANDARD 20)

#link_directories(/usr/local/lib)

file(GLOB SOURCES
        "../../include/*.h"
        "../../src/*.cpp")

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(upload ${SOURCES} main.cpp)
target_link_libraries(upload libtorrent-rasterbar.so Threads::Threads)