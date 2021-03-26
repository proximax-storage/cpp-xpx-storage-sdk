cmake_minimum_required(VERSION 3.9)
project(libtorrenttest)

set(CMAKE_CXX_STANDARD 20)

add_executable(libtorrenttest libtorrenttest.cpp)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(libtorrenttest /usr/local/lib/libtorrent-rasterbar.dylib.2.0 drive)
else()
    target_link_libraries(libtorrenttest torrent-rasterbar Threads::Threads drive)
endif()
