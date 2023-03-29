cmake_minimum_required(VERSION 3.9)
project(cpp-xpx-storage-sdk VERSION 1.0.0)
include(CMakeGlobalSettings.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(ABSL_PROPAGATE_CXX_STD ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random REQUIRED)
else()
    find_package(Boost COMPONENTS atomic system date_time regex timer chrono log thread filesystem program_options random stacktrace_backtrace REQUIRED)
    add_definitions(-DBOOST_STACKTRACE_USE_BACKTRACE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DSHOW_BACKTRACE")
endif()
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

include(FetchContent)

set(gRPC_SSL_PROVIDER "package" CACHE STRING "Provider of absl library")
set(gRPC_SSL_PROVIDER "package" CACHE STRING "Provider of ssl library")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_TMP ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../lib)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS_COPY ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ON)
if (NOT DEFINED SIRIUS_GRPC_VERSION)
    set(SIRIUS_GRPC_VERSION v1.52.1)
endif()
FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG        ${SIRIUS_GRPC_VERSION})
FetchContent_MakeAvailable(grpc)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_COPY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY_TMP})

# Since FetchContent uses add_subdirectory under the hood, we can use
# the grpc targets directly from this build.
set(_PROTOBUF_LIBPROTOBUF libprotobuf)
set(_REFLECTION grpc++_reflection)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
set(_GRPC_GRPCPP grpc++)
if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
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

add_subdirectory(supercontract-server)
add_subdirectory(messenger-server)
add_subdirectory(src)
add_subdirectory(replicator-service)

if (XPX_STORAGE_SDK_NOT_BUILD_EXAMPLES)
else()
    add_subdirectory(examples)
endif()
