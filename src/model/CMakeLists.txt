cmake_minimum_required(VERSION 3.9)
add_definitions(-w)
storage_sdk_shared_library_target(model)
target_link_libraries(model storage-crypto utils)

