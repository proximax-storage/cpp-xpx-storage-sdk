cmake_minimum_required(VERSION 3.2)
project(replicator)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

add_definitions(-DRPC_DRIVE)
include_directories(SYSTEM "../../../libtorrent/include" "../../rpclib/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(replicator replicator.cpp ${SOURCES})

#target_precompile_headers(replicator PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(replicator
        ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
        ${CMAKE_SOURCE_DIR}/rpclib/_build/librpc.a
        )
#        /usr/local/opt/openssl/lib/libssl.dylib
#        /usr/local/opt/openssl/lib/libcrypto.dylib
#        "-framework CoreFoundation" "-framework SystemConfiguration"
#        )
else()
    target_link_libraries(replicator libtorrent-rasterbar.so Threads::Threads)
endif()
