cmake_minimum_required(VERSION 3.2)
project(supercontract-server-example)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)

add_executable(supercontract-server-example main.cpp)

target_include_directories(supercontract-server-example PUBLIC)

target_link_libraries(supercontract-server-example drive storage-crypto torrent-sirius Threads::Threads)


