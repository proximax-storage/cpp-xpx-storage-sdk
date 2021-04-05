cmake_minimum_required(VERSION 3.2)
project(downloadFile)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "../../src/drive/*.cpp")

include_directories( PRIVATE "../../include/drive" )

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(downloadFile ${SOURCES} downloadFile.cpp)

target_precompile_headers(downloadFile PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(downloadFile /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
else()
    target_link_libraries(downloadFile libtorrent-rasterbar.so Threads::Threads)
endif()
