cmake_minimum_required(VERSION 3.9)
project(cpp-xpx-storage-sdk VERSION 1.0.0)
include(CMakeGlobalSettings.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

### setup boost
find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

include_directories(SYSTEM "cereal/include")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # boost from brew has an issue linking to -licudata
    # https://stackoverflow.com/questions/65380307/os-x-library-not-found-for-licudata
    # this needs to be defined before add_library
    #link_directories(/usr/local/opt/icu4c/lib/)

    link_directories(/usr/local/opt/openssl@1.1/lib)
    include_directories(SYSTEM "/usr/local/include/" )
endif()

#@#add_subdirectory(src)
add_subdirectory(examples)
