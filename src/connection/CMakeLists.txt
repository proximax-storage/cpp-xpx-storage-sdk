cmake_minimum_required(VERSION 3.9)

storage_sdk_library_target(connection)
target_link_libraries(connection net ionet nodediscovery)
