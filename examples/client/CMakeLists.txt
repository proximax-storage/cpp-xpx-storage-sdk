cmake_minimum_required(VERSION 3.2)
project(client)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

add_definitions(-DRPC_DRIVE)
include_directories(SYSTEM "../../../libtorrent/include" "../../rpclib/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(client client.cpp ${SOURCES})

#target_precompile_headers(client PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(client
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
#    target_link_libraries(client libtorrent-rasterbar.so Threads::Threads)
    target_link_libraries(client
            /home/newman/projects/libtorrent/_build/libtorrent-sirius.so
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            Threads::Threads)
endif()
