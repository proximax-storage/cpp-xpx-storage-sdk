cmake_minimum_required(VERSION 3.2)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(TARGET_NAME replicator-service)
SET(CMAKE_CXX_FLAGS_DEBUG "-g3")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
storage_sdk_executable(${TARGET_NAME})

add_definitions(-DSIRIUS_DRIVE_MULTI)
IF(APPLE)
    #add_definitions(-D_GNU_SOURCE)
    add_definitions(-DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED)

    set(BOOST_LIB_DIR "/usr/local/lib")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
    set(OPENSSL_LIBRARIES "/usr/local/opt/openssl/lib")
endif()

include_directories(SYSTEM "../libtorrent/include")
include_directories(../include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

#target_link_libraries(testSingleNodeDiscovery connection crypto ionet net model nodediscovery)
set_property(TARGET ${TARGET_NAME} PROPERTY ENABLE_EXPORTS ON)
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(${TARGET_NAME} drive storage-crypto torrent-sirius easyloggingpp)
else()
    target_link_libraries(${TARGET_NAME} drive storage-crypto torrent-sirius easyloggingpp Threads::Threads)
endif()

storage_sdk_target(${TARGET_NAME})
