cmake_minimum_required(VERSION 3.9)

find_package(OpenSSL REQUIRED)

storage_sdk_shared_library_target(supercontract-server)

target_include_directories(supercontract-server PUBLIC ../include)
target_include_directories(supercontract-server PUBLIC SYSTEM ${Boost_INCLUDE_DIR})

target_link_libraries(supercontract-server utils OpenSSL::SSL)
