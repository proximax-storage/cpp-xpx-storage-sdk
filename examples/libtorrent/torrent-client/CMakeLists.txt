cmake_minimum_required(VERSION 3.9)
project(torrent_client)

set(CMAKE_CXX_STANDARD 20)

add_executable(torrent-client main.cpp)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(torrent-client /usr/local/lib/libtorrent-rasterbar.dylib.2.0 drive)
else()
    target_link_libraries(torrent-client torrent-rasterbar Threads::Threads drive)
endif()