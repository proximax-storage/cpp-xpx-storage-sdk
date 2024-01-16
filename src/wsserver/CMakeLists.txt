cmake_minimum_required(VERSION 3.9)
set(CMAKE_CXX_STANDARD 20)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(wsserver)