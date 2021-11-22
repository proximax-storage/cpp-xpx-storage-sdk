cmake_minimum_required(VERSION 3.2)
project(replicator2)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DRPC_DRIVE)
include_directories(SYSTEM "../../../libtorrent/include" "../../rpclib/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(replicator2 replicator.cpp)

#target_precompile_headers(replicator PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(replicator2
        ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
        ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
        )
#        /usr/local/opt/openssl/lib/libssl.dylib
#        /usr/local/opt/openssl/lib/libcrypto.dylib
#        "-framework CoreFoundation" "-framework SystemConfiguration"
#        )
else()
    target_link_libraries(replicator2
            drive
            crypto
            librpc.a
            Threads::Threads)
endif()
