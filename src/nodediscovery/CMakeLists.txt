cmake_minimum_required(VERSION 3.9)

storage_sdk_library_target(nodediscovery)
target_link_libraries(nodediscovery utils model ionet connection)

