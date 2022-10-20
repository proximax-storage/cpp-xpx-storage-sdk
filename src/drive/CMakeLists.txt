cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)

if (APPLE)
    add_definitions(-D_GNU_SOURCE)
    include_directories(${PROJECT_SOURCE_DIR}/libtorrent/include)
    target_link_libraries(drive supercontract-server storage-crypto torrent-sirius)
else()
	target_link_libraries(drive supercontract-server storage-crypto torrent-sirius)
endif()
