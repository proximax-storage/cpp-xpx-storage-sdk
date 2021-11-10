#cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)
target_link_libraries(drive librpc.a torrent-sirius)