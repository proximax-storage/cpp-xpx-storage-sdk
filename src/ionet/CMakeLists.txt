cmake_minimum_required(VERSION 3.10)

include_directories(../../include/)

storage_sdk_shared_library_target(ionet)
target_link_libraries(ionet storage-crypto model)