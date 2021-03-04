cmake_minimum_required(VERSION 3.9)
project(cpp-xpx-storage-sdk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    "include/FileTransmitter/*.h"
    "src/*.cpp")

include_directories( SYSTEM "cereal/include" )
include_directories( PRIVATE "include" )


add_library(xpxstoragesdk SHARED
    src/ActionList.cpp
    src/FileTransmitter.cpp
    )


set_target_properties(xpxstoragesdk PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(xpxstoragesdk)

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
