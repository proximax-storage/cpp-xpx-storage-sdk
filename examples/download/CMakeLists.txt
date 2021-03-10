cmake_minimum_required(VERSION 3.2)
project(download)

set(CMAKE_CXX_STANDARD 20)

#link_directories(/usr/local/lib)

file(GLOB SOURCES
        "../../include/*.h"
        "../../src/*.cpp")

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(download ${SOURCES} main.cpp)
target_link_libraries(download libtorrent-rasterbar.so Threads::Threads)