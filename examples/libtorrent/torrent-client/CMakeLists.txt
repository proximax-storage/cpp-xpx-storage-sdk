cmake_minimum_required(VERSION 3.16)
project(torrent_client)

set(CMAKE_CXX_STANDARD 20)

link_directories(/usr/local/lib)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(torrent_client main.cpp)
target_link_libraries(torrent_client libtorrent-rasterbar.so Threads::Threads)