include(FetchContent)
set(gRPC_SSL_PROVIDER "package" CACHE STRING "Provider of ssl library")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../lib)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS ON)
if (NOT DEFINED SIRIUS_GRPC_VERSION)
    set(SIRIUS_GRPC_VERSION v1.49.1)
endif ()
FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG ${SIRIUS_GRPC_VERSION})
FetchContent_MakeAvailable(grpc)

# Since FetchContent uses add_subdirectory under the hood, we can use
# the grpc targets directly from this build.
set(_PROTOBUF_LIBPROTOBUF libprotobuf PARENT_SCOPE)
set(_REFLECTION grpc++_reflection PARENT_SCOPE)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc> PARENT_SCOPE)
set(_GRPC_GRPCPP grpc++ PARENT_SCOPE)
if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin> PARENT_SCOPE)
endif()