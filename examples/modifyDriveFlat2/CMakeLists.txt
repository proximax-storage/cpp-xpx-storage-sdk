cmake_minimum_required(VERSION 3.2)
project(modifyDriveFlat2)
#add_definitions(-DSIRIUS_DRIVE_MULTI)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

include_directories(SYSTEM "../../../libtorrent/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDriveFlat2 modifyDriveFlat2.cpp ${SOURCES})

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(modifyDriveFlat2
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(modifyDriveFlat2 ${CMAKE_SOURCE_DIR}/libtorrent/_build/libtorrent-sirius.so Threads::Threads)
endif()
