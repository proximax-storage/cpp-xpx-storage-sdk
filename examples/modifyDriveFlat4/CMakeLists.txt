cmake_minimum_required(VERSION 3.2)
project(modifyDriveFlat4)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp ../../src/crypto/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDriveFlat4 modifyDriveFlat4.cpp ${SOURCES})

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(modifyDriveFlat4
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            #${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
            storage-crypto
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(modifyDriveFlat4 drive storage-crypto emulator xpx.rpc torrent-sirius Threads::Threads)
endif()
