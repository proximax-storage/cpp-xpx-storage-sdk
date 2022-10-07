cmake_minimum_required(VERSION 3.2)
project(modifyDriveFlat5)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDriveFlat5 modifyDriveFlat5.cpp utils.cpp ${SOURCES})

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

add_definitions(-DDEBUG_OFF_CATAPULT)
add_definitions(-DMINI_SIGNATURE)
add_definitions(-DENABLE_VERIFICATIONS)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D_GNU_SOURCE)
    target_link_libraries(modifyDriveFlat5
            #${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
#            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            storage-crypto
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(modifyDriveFlat5 drive storage-crypto torrent-sirius Threads::Threads)
endif()
