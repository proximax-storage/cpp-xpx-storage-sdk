cmake_minimum_required(VERSION 3.9)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
storage_sdk_library_target(thread)
target_link_libraries(thread ionet utils)
