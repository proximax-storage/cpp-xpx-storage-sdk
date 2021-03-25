cmake_minimum_required(VERSION 3.9)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(drive)
add_subdirectory(external)
add_subdirectory(connection)
add_subdirectory(crypto)
add_subdirectory(ionet)
add_subdirectory(net)
add_subdirectory(thread)
add_subdirectory(utils)
