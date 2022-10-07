cmake_minimum_required(VERSION 3.2)
project(extensionEmulator)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DRPC_DRIVE)

include_directories(../../include)
include_directories(../../libtorrent)
include_directories(../../rpclib)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(extensionEmulator extensionEmulator.cpp)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(extensionEmulator
        ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
        ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
        )
#        /usr/local/opt/openssl/lib/libssl.dylib
#        /usr/local/opt/openssl/lib/libcrypto.dylib
#        "-framework CoreFoundation" "-framework SystemConfiguration"
#        )
else()
    target_link_libraries(extensionEmulator sdk_external storage-crypto emulator Threads::Threads)
endif()
