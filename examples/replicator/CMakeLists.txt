cmake_minimum_required(VERSION 3.2)
project(replicator)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "../../src/drive/*.cpp" "../../include/drive/*.h")

include_directories( PRIVATE "../../include/drive" )

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(replicator replicator.cpp ${SOURCES})

target_precompile_headers(replicator PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(replicator /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
else()
    target_link_libraries(replicator libtorrent-rasterbar.so Threads::Threads)
endif()
