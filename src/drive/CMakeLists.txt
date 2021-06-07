#cmake_minimum_required(VERSION 3.9)

set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)

#include_directories( PRIVATE "../../include/drive" )

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(drive
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            )
#            "-framework CoreFoundation" "-framework SystemConfiguration")
else()
    target_link_libraries(drive
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.so
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            Threads::Threads)
endif()

#include_directories(${PROJECT_SOURCE_DIR})
