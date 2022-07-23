cmake_minimum_required(VERSION 3.2)
project(replicator-service)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DSIRIUS_DRIVE_MULTI)
IF(APPLE)
    #add_definitions(-D_GNU_SOURCE)
    add_definitions(-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)
endif()

file(GLOB SOURCES ../src/drive/*.cpp)

include_directories(SYSTEM "../libtorrent/include")
include_directories(../include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(replicator-service replicator-service.cpp ${SOURCES})

#target_link_libraries(testSingleNodeDiscovery connection crypto ionet net model nodediscovery)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(replicator-service crypto
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            )
else()
    target_link_libraries(replicator-service crypto libtorrent-rasterbar.so Threads::Threads)
endif()
