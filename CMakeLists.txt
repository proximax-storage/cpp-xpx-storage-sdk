cmake_minimum_required(VERSION 3.9)
project(cpp-xpx-storage-sdk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)


#-----------------------------------------------------------------------------
# required dependencies
#-----------------------------------------------------------------------------


find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)


#-----------------------------------------------------------------------------
# options and features
#-----------------------------------------------------------------------------


option(XPX_STORAGE_SDK_BUILD_STATIC "Build static version of library." OFF)
option(XPX_STORAGE_SDK_BUILD_TESTS "Build tests." OFF)
option(XPX_STORAGE_SDK_BUILD_EXAMPLES "Build examples." OFF)


#-----------------------------------------------------------------------------
# sources
#-----------------------------------------------------------------------------

file(GLOB_RECURSE sources
    "include/*.h"
    "src/*.cpp")

include_directories(src)
include_directories(src/external)
include_directories( SYSTEM "cereal/include" )
include_directories( PRIVATE "include" )

# Boost
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS atomic system date_time regex
        timer chrono log thread filesystem program_options random REQUIRED)

include_directories(SYSTEM ${Boost_INCLUDE_DIR})
LINK_DIRECTORIES( ${Boost_LIBRARY_DIR} )

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    include_directories( SYSTEM "/usr/local/include/" )
endif()

add_subdirectory(src/sirius)
add_subdirectory(src/netio)
add_subdirectory(src/external)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # boost from brew has an issue linking to -licudata
    # https://stackoverflow.com/questions/65380307/os-x-library-not-found-for-licudata
    # this needs to be defined before add_library
    link_directories(/usr/local/opt/icu4c/lib/)
endif()

add_library(xpxstoragesdk SHARED ${sources} ${catapult} ${netio})

set_target_properties(xpxstoragesdk PROPERTIES LINKER_LANGUAGE CXX)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

    link_directories(/usr/local/opt/openssl@1.1/lib)

    target_link_libraries(xpxstoragesdk PRIVATE
        #/usr/local/lib/libtorrent.a
        /usr/local/lib/libtorrent-rasterbar.dylib.2.0.0
        /usr/local/opt/openssl@1.1/lib/libssl.dylib
        /usr/local/opt/openssl@1.1/lib/libcrypto.dylib
        "-framework CoreFoundation" "-framework SystemConfiguration"
        ${Boost_LIBRARIES}
        sirius
        netio
        external)#Threads::Threads)
else()
    target_link_libraries(xpxstoragesdk libtorrent-rasterbar.so Threads::Threads)
endif()

#-----------------------------------------------------------------------------
# tests
#-----------------------------------------------------------------------------

#if (XPX_STORAGE_SDK_BUILD_TESTS)
#	include(CTest)
#	enable_testing()
#	add_subdirectory(tests)
#endif()


#-----------------------------------------------------------------------------
# examples
#-----------------------------------------------------------------------------
#if (XPX_STORAGE_SDK_BUILD_EXAMPLES)
#	add_executable(get_block ${CMAKE_CURRENT_SOURCE_DIR}/examples/get_block.cpp)
#	target_link_libraries(get_block
#                        xpxstoragesdk
#			CONAN_PKG::rapidjson
#			CONAN_PKG::boost
#			CONAN_PKG::OpenSSL)
#endif()

add_subdirectory(examples)
