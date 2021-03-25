cmake_minimum_required(VERSION 3.9)

include_directories(../../external)

storage_sdk_library_target(crypto)
target_link_libraries(crypto utils external)
