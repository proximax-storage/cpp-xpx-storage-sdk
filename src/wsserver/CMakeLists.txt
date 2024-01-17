cmake_minimum_required(VERSION 3.9)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(wsserver)
target_link_libraries(wsserver)