cmake_minimum_required(VERSION 3.2)
project(modifyDriveFlat)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

include_directories(SYSTEM "../../../libtorrent/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(modifyDriveFlat modifyDriveFlat.cpp ${SOURCES})

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(modifyDriveFlat
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(modifyDriveFlat libtorrent-rasterbar.so Threads::Threads)
endif()
