cmake_minimum_required(VERSION 3.2)
project(streamExample)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/drive/*.cpp)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

#add_executable(streamExample streamExample.cpp ${SOURCES})
add_executable(streamExample offchainTest.cpp ${SOURCES})

#configure_file( ${CMAKE_CURRENT_SOURCE_DIR}../../../js-xpx-streaming-app/client/player.html
#        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/js/player.html COPYONLY)

add_definitions(-DDEBUG_OFF_CATAPULT)
add_definitions(-DMINI_SIGNATURE)
add_definitions(-DENABLE_VERIFICATIONS)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D_GNU_SOURCE)
    target_link_libraries(streamExample drive crypto torrent-sirius )
    #        /usr/local/opt/openssl/lib/libssl.dylib
    #        /usr/local/opt/openssl/lib/libcrypto.dylib
    #        "-framework CoreFoundation" "-framework SystemConfiguration"
    #        )
else()
    target_link_libraries(streamExample drive crypto torrent-sirius Threads::Threads)
endif()
