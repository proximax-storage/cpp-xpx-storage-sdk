cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
add_definitions(-w)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)

if (APPLE)
    add_definitions(-D_GNU_SOURCE)
    include_directories(${PROJECT_SOURCE_DIR}/libtorrent/include)
endif()

if (DEFINED SKIP_GRPC)
    target_link_libraries(drive storage-crypto torrent-sirius easyloggingpp)
else()
    target_link_libraries(drive supercontract-server messenger-server storage-crypto torrent-sirius easyloggingpp)
endif()
