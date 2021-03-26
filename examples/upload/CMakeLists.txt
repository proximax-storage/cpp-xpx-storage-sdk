cmake_minimum_required(VERSION 3.9)
project(upload)

set(CMAKE_CXX_STANDARD 20)

add_executable(upload main.cpp)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(upload /usr/local/lib/libtorrent-rasterbar.dylib.2.0.0 drive)
else()
    target_link_libraries(upload torrent-rasterbar Threads::Threads drive)
endif()
