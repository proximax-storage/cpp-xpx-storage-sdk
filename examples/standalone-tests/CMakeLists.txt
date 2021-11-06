cmake_minimum_required(VERSION 3.2)
project(standalone-tests)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp ../../src/crypto/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(standalone-tests runner.cpp utils.h TestEnvironment.h opinionReceivedRootCalculated.h opinionReceivedRootNotCalculated.h approvalReceivedRootCalculated.h approvalReceivedRootNotCalculated.h ${SOURCES} utils.cpp TestEnvironment.cpp)

#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(standalone-tests
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            #${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
            crypto
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(standalone-tests drive crypto ${CMAKE_SOURCE_DIR}/libtorrent/_build/libtorrent-sirius.so Threads::Threads)
endif()
