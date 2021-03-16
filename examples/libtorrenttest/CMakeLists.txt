cmake_minimum_required(VERSION 3.2)
project(libtorrenttest)

set(CMAKE_CXX_STANDARD 20)

#link_directories(/usr/local/lib)

file(GLOB SOURCES
        "../../include/*.h"
        "../../src/*.cpp")

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(libtorrenttest ${SOURCES} libtorrenttest.cpp)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(libtorrenttest /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
else()
    target_link_libraries(libtorrenttest libtorrent-rasterbar.so Threads::Threads)
endif()
