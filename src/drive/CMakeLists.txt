#cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D_GNU_SOURCE)
    include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/libtorrent/include)
    target_link_libraries(drive supercontract-server crypto torrent-sirius)

#    target_link_libraries(drive
#            #${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
#            ${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
#            crypto
#            )
#            "-framework CoreFoundation" "-framework SystemConfiguration")
else()
	target_link_libraries(drive supercontract-server crypto torrent-sirius)
endif()
