cmake_minimum_required(VERSION 3.2)
project(modifyDrive)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "../../src/drive/*.cpp" "../../include/drive/*.h")

include_directories( PRIVATE "../../include/drive" )

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDrive modifyDrive.cpp ${SOURCES})

target_precompile_headers(modifyDrive PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(modifyDrive /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
else()
    target_link_libraries(modifyDrive libtorrent-rasterbar.so Threads::Threads)
endif()
