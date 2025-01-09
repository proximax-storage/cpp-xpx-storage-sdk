cmake_minimum_required(VERSION 3.9)

find_package(OpenSSL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
find_package(Protobuf REQUIRED)
find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin PATHS /usr/bin /usr/local/bin)
find_program(_PROTOBUF_PROTOC protoc)

storage_sdk_library_target(supercontract-server)

storage_sdk_proto(storageServer ${CMAKE_CURRENT_SOURCE_DIR} "")
target_link_libraries(supercontract-server storageServer_sirius_grpc_proto)

target_include_directories(supercontract-server PUBLIC ../include ../../include)
target_include_directories(supercontract-server PUBLIC SYSTEM ${Boost_INCLUDE_DIR})

IF(APPLE)
    set(BOOST_LIB_DIR "/usr/local/lib")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
    set(OPENSSL_LIBRARIES "/usr/local/opt/openssl/lib")
else()
    target_link_libraries(supercontract-server utils OpenSSL::SSL)
endif()

# Needed for proto linkage
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
