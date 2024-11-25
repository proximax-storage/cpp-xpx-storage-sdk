cmake_minimum_required(VERSION 3.10)

include_directories(../../external)

storage_sdk_shared_library_target(storage-crypto)
target_link_libraries(storage-crypto utils sdk_external)
