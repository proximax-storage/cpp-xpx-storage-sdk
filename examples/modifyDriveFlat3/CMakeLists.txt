cmake_minimum_required(VERSION 3.2)
project(modifyDriveFlat3)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDriveFlat3 modifyDriveFlat3.cpp ${SOURCES})

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

add_definitions(-DDEBUG_OFF_CATAPULT)
add_definitions(-DMINI_SIGNATURE)
add_definitions(-DENABLE_VERIFICATIONS)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(modifyDriveFlat3
            #${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
#            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            crypto
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(modifyDriveFlat3 drive crypto torrent-sirius Threads::Threads)
endif()
