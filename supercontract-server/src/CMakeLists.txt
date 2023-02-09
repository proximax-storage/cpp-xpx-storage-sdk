cmake_minimum_required(VERSION 3.9)

find_package(OpenSSL REQUIRED)

storage_sdk_library_target(supercontract-server)

storage_sdk_proto(storageServer "")
target_link_libraries(supercontract-server storageServer_storage_grpc_proto)

target_include_directories(supercontract-server PUBLIC ../include ../../include)
target_include_directories(supercontract-server PUBLIC SYSTEM ${Boost_INCLUDE_DIR})

target_link_libraries(supercontract-server utils OpenSSL::SSL)
