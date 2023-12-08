cmake_minimum_required(VERSION 3.2)
project(messenger-echo-server)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(messenger-echo-server main.cpp)

target_include_directories(messenger-echo-server PUBLIC)

target_link_libraries(messenger-echo-server messenger-server Threads::Threads)


