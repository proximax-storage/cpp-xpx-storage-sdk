cmake_minimum_required(VERSION 3.9)

storage_sdk_shared_library_target(connection)
target_link_libraries(connection ionet utils net nodediscovery)
