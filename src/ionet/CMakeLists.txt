cmake_minimum_required(VERSION 3.9)

storage_sdk_library_target(ionet)
target_link_libraries(ionet thread crypto)
