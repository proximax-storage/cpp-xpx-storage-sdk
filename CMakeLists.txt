cmake_minimum_required(VERSION 3.9)
project(cpp-xpx-storage-sdk VERSION 1.0.0)
include(CMakeGlobalSettings.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(BUILD_STATIC_LIBS "Build the static library" ON)
option(BUILD_SHARED_LIBS "Build the shared library" OFF)

### setup boost
find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

include_directories(SYSTEM "cereal/include")
include_directories(SYSTEM "libtorrent/include")
include_directories(SYSTEM "rpclib/include")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_VERBOSE_MAKEFILE ON)
    
    # boost from brew has an issue linking to -licudata
    # https://stackoverflow.com/questions/65380307/os-x-library-not-found-for-licudata
    # this needs to be defined before add_library
    #link_directories(/usr/local/opt/icu4c/lib/)
    #set(CMAKE_CXX_FLAGS "--sysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.0.sdk ${CMAKE_CXX_FLAGS}")
    #set(CMAKE_CXX_FLAGS "-std=c++20 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.0.sdk ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "-std=c++20 -isysroot                                             /Library/Developer/CommandLineTools/SDKs/MacOSX12.0.sdk ${CMAKE_CXX_FLAGS}")

#    set(CMAKE_OSX_SYSROOT /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.0.sdk )
#
#    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")

    #link_directories(/usr/local/opt/openssl@1.1/lib)
    #include_directories(SYSTEM "/usr/local/include/" "/Library/Developer/CommandLineTools/SDKs/MacOSX12.0.sdk/usr/include/")
    #include_directories(SYSTEM "/Library/Developer/CommandLineTools/SDKs/MacOSX12.0.sdk/usr/include/")

endif()

add_subdirectory(src)

add_subdirectory(examples)
