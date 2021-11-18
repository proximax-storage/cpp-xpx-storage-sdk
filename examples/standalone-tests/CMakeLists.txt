cmake_minimum_required(VERSION 3.2)
project(standalone-tests)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp ../../src/crypto/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)
find_package(GTest REQUIRED) # Find the google testing framework on your system

file(GLOB TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test*.cpp")
add_executable(standalone-tests utils.h utils.cpp TestEnvironment.h ${TEST_FILES})

include_directories(${GTEST_INCLUDE_DIRS})
#target_precompile_headers(modifyDriveFlat PRIVATE pch.h)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(standalone-tests
            ${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            #${CMAKE_SOURCE_DIR}/cpp-xpx-storage-sdk/libtorrent/_build/Debug/libtorrent-sirius.dylib
            #${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
            drive
            crypto
            ${GTEST_LIBRARIES}
            /usr/local/lib/libgtest_main.a
            )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(standalone-tests drive crypto ${CMAKE_SOURCE_DIR}/libtorrent/_build/libtorrent-sirius.so Threads::Threads ${GTEST_LIBRARIES} gtest_main)
endif()
