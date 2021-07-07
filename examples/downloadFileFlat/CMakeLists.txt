cmake_minimum_required(VERSION 3.2)
project(downloadFileFlat)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

include_directories(SYSTEM "../../../libtorrent/include")
include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(downloadFileFlat ${SOURCES} downloadFileFlat.cpp)

target_precompile_headers(downloadFileFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} /usr/local/Cellar/openssl@1.1/1.1.1i/)
    set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libcrypto.dylib CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY ${OPENSSL_ROOT_DIR}/lib/libssl.dylib CACHE FILEPATH "" FORCE)

    #link_directories(/usr/local/opt/icu4c/lib/)
    link_directories(/usr/local/opt/openssl@1.1/lib)

    target_link_libraries(downloadFileFlat  ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib )
            #        /usr/local/opt/openssl/lib/libssl.dylib
            #        /usr/local/opt/openssl/lib/libcrypto.dylib
            #        "-framework CoreFoundation" "-framework SystemConfiguration"
            #        )
else()
    target_link_libraries(downloadFileFlat ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.so Threads::Threads)
endif()
