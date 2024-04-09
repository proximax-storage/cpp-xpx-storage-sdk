cmake_minimum_required(VERSION 3.9)
project(wsserverapp)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES ../../src/wsserver/*.cpp)

include_directories(${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(wsserverapp server.cpp ${SOURCES})

target_link_libraries(wsserverapp PRIVATE ${Boost_LIBRARIES} wsserver Threads::Threads OpenSSL::SSL)