#cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(drive)

#include_directories( PRIVATE "../../include/drive" )

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(drive
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            crypto
            )
#            "-framework CoreFoundation" "-framework SystemConfiguration")
else()
#    message('${CMAKE_SOURCE_DIR}')
#    message('${PROJECT_SOURCE_DIR}')
    target_link_libraries(drive
            ${PROJECT_SOURCE_DIR}/libtorrent/_build/libtorrent-sirius.so
            ${PROJECT_SOURCE_DIR}/rpclib/build/librpc.a
            Threads::Threads)
endif()

#include_directories(${PROJECT_SOURCE_DIR})
