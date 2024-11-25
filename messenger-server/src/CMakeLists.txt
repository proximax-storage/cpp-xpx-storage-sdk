cmake_minimum_required(VERSION 3.10)

find_package(OpenSSL REQUIRED)

storage_sdk_library_target(messenger-server)

storage_sdk_proto(messengerServer "")
target_link_libraries(messenger-server messengerServer_sirius_grpc_proto)

target_include_directories(messenger-server PUBLIC ../include ../../include)
target_include_directories(messenger-server PUBLIC SYSTEM ${Boost_INCLUDE_DIR})

target_link_libraries(messenger-server utils OpenSSL::SSL)

# Needed for proto linkage
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")