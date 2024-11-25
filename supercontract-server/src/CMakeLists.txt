cmake_minimum_required(VERSION 3.10)

find_package(OpenSSL REQUIRED)

storage_sdk_library_target(supercontract-server)

storage_sdk_proto(storageServer "")
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
