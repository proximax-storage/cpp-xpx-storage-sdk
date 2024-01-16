# cmake_minimum_required(VERSION 3.9)
# set(CMAKE_CXX_STANDARD 20)

# include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

# # add_subdirectory(wsserver)

cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
add_definitions(-w)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(wsserver)

