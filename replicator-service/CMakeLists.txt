cmake_minimum_required(VERSION 3.2)

set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME replicator-service)
storage_sdk_executable(${TARGET_NAME})

add_definitions(-DSIRIUS_DRIVE_MULTI)
IF(APPLE)
    #add_definitions(-D_GNU_SOURCE)
    add_definitions(-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
endif()

include_directories(SYSTEM "../libtorrent/include")
include_directories(../include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

#target_link_libraries(testSingleNodeDiscovery connection crypto ionet net model nodediscovery)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(${TARGET_NAME} drive crypto
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            )
else()
    target_link_libraries(${TARGET_NAME} drive crypto torrent-sirius Threads::Threads)
endif()
storage_sdk_target(${TARGET_NAME})