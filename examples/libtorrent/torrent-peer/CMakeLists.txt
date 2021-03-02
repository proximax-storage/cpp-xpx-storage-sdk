cmake_minimum_required(VERSION 3.16)
project(torrent-peer)

set(CMAKE_CXX_STANDARD 20)

link_directories(/usr/local/lib)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(torrent-peer main.cpp)
target_link_libraries(torrent-peer libtorrent-rasterbar.so Threads::Threads)