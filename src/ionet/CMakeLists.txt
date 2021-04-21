cmake_minimum_required(VERSION 3.9)

include_directories(../../include/)

storage_sdk_library_target(ionet)
target_link_libraries(ionet crypto model)