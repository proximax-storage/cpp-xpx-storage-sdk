cmake_minimum_required(VERSION 3.9)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

project(cpp-xpx-storage-sdk VERSION 1.0.0)
include(CMakeGlobalSettings.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS ON)
set(ABSL_PROPAGATE_CXX_STD ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT DEFINED CPP_XPX_CHAIN)
    set(SKIP_GRPC ON)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DELPP_NO_DEFAULT_LOG_FILE")
    add_definitions(-DUSE_ELPP)
endif()

if (DEFINED SKIP_SUPERCONTRACT)
    set(SKIP_GRPC ON)
endif()

if (DEFINED SKIP_GRPC)
    add_compile_definitions(SKIP_GRPC)

    if(MSVC)
        #add_compile_options(/bigobj /W3 /WX)
    else()
        add_compile_options(-Werror=return-type)
    endif()
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(WIN32)
        set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
        set(BUILD_SHARED_LIBS TRUE)
endif()

include_directories(include)

### setup boost
if (WIN32 AND "${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
    SET(Boost_NAMESPACE ${Boost_NAMESPACE} "libboost")
    SET(Boost_ARCHITECTURE "-x64")
endif()

set(Boost_NO_WARN_NEW_VERSIONS 1)

if((CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "16.0.0") OR
    (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "11"))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-enum-constexpr-conversion")
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random REQUIRED)
elseif(WIN32)
    find_package(Boost COMPONENTS atomic system date_time regex timer chrono log log_setup thread filesystem program_options random REQUIRED)
    set(BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE "${Bost_INCLUDE_DIR}boost/stacktrace.hpp")
else()
    find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random stacktrace_backtrace REQUIRED)
    add_definitions(-DBOOST_STACKTRACE_USE_BACKTRACE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DSHOW_BACKTRACE")
endif()
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

if (NOT DEFINED SKIP_GRPC)
    storage_sdk_third_party_lib(cmake/grpc.cmake)
endif()

include_directories(cereal/include)
add_subdirectory(libtorrent)
include_directories(libtorrent/include)

if (XPX_STORAGE_SDK_BUILD_EMULATOR)
    add_subdirectory(cpp-xpx-rpclib)
    include_directories(${PROJECT_SOURCE_DIR}/cpp-xpx-storage-sdk/cpp-xpx-rpclib/include)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_VERBOSE_MAKEFILE ON)
    add_definitions(-DBOOST_ASIO_HAS_STD_INVOKE_RESULT)

    # boost from brew has an issue linking to -licudata
    # https://stackoverflow.com/questions/65380307/os-x-library-not-found-for-licudata
    # this needs to be defined before add_library
    #link_directories(/usr/local/opt/icu4c/lib/)
    set(CMAKE_CXX_FLAGS "-std=c++20 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk ${CMAKE_CXX_FLAGS}")
    #set(CMAKE_CXX_FLAGS "-std=c++20 -isysroot  /Library/Developer/CommandLineTools/SDKs/MacOSX12.1.sdk ${CMAKE_CXX_FLAGS}")
    #set(CMAKE_CXX_FLAGS "-std=c++20 -isysroot  /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk ${CMAKE_CXX_FLAGS}")

#    set(CMAKE_OSX_SYSROOT /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.0.sdk )
#
#    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")

    #link_directories(/usr/local/opt/openssl@1.1/lib)
    #include_directories(SYSTEM "/usr/local/include/" "/Library/Developer/CommandLineTools/SDKs/MacOSX12.0.sdk/usr/include/")
    #include_directories(SYSTEM "/Library/Developer/CommandLineTools/SDKs/MacOSX12.0.sdk/usr/include/")

#    add_subdirectory(examples)
endif()

if (NOT DEFINED SKIP_GRPC)
    add_subdirectory(supercontract-server)
    add_subdirectory(messenger-server)
endif()

add_subdirectory(src)

if(NOT WIN32)
    add_subdirectory(replicator-service)
endif()

if (XPX_STORAGE_SDK_NOT_BUILD_EXAMPLES)
else()
    add_subdirectory(examples)
endif()

