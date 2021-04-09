cmake_minimum_required(VERSION 3.2)
project(downloadFile)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "../../src/drive/*.cpp")

include_directories( PRIVATE "../../include/drive" )

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(downloadFile ${SOURCES} downloadFile.cpp)

target_precompile_headers(downloadFile PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} /usr/local/Cellar/openssl@1.1/1.1.1i/)
    set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.dylib CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libssl.dylib CACHE FILEPATH "" FORCE)

    link_directories(/usr/local/opt/icu4c/lib/)
    link_directories(/usr/local/opt/openssl@1.1/lib)

    target_link_libraries(downloadFile
#        /usr/local/lib/libtorrent-rasterbar.dylib.2.0
        /Users/alex/Proj/libtorrent/bin/Debug/libtorrent-rasterbar.a
#        /Users/alex/Proj/libtorrent/bin/Debug/libtorrent-rasterbar.dylib
        /usr/local/opt/openssl@1.1/lib/libcrypto.dylib
        /usr/local/opt/openssl@1.1/lib/libssl.dylib
        "-framework CoreFoundation" "-framework SystemConfiguration" )
else()
    target_link_libraries(downloadFile libtorrent-rasterbar.so Threads::Threads)
endif()
