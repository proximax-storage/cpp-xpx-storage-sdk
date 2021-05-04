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
        #/Users/alex/Proj/libtorrent/bin/darwin-12.0.0/release/crypto-openssl/cxxstd-17-iso/debug-symbols-on/threading-multi/libtorrent-rasterbar.dylib.2.0.3
        /Users/alex/Proj/libtorrent/_bin/libtorrent-rasterbar.2.0.dylib
        #/Users/alex/Proj/libtorrent/_bin_rel/libtorrent-rasterbar.dylib
        #/Users/alex/Proj/libtorrent/bin/Debug/libtorrent-rasterbar.2.0.3.dylib
        )
#        /usr/local/opt/openssl/lib/libssl.dylib
#        /usr/local/opt/openssl/lib/libcrypto.dylib
#        "-framework CoreFoundation" "-framework SystemConfiguration"
#        )
#    target_link_libraries(modifyDriveFlat /usr/local/lib/libtorrent-rasterbar.dylib.2.0)
#    target_link_libraries(modifyDriveFlat /Users/alex/Proj/libtorrent-rasterbar-2.0.0/bin/Debug/libtorrent-rasterbar.2.0.0.dylib)
else()
    target_link_libraries(modifyDriveFlat libtorrent-rasterbar.so Threads::Threads)
endif()
