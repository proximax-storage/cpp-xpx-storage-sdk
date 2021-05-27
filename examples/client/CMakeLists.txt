cmake_minimum_required(VERSION 3.2)
project(client)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "../../src/drive/*.cpp" "../../include/drive/*.h")

include_directories( PRIVATE "../../include/drive" )

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(client client.cpp ${SOURCES})

target_precompile_headers(client PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(client /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
else()
    target_link_libraries(client libtorrent-rasterbar.so Threads::Threads)
endif()
