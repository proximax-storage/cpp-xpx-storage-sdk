cmake_minimum_required(VERSION 3.2)
project(test_timeouts)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(test_timeouts test_20000_timeouts.cpp)

add_definitions(-DDEBUG_OFF_CATAPULT)
add_definitions(-DMINI_SIGNATURE)
add_definitions(-DENABLE_VERIFICATIONS)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D_GNU_SOURCE)
    target_link_libraries(test_timeouts drive storage-crypto torrent-sirius )
else()
    target_link_libraries(test_timeouts drive storage-crypto torrent-sirius Threads::Threads)
endif()
