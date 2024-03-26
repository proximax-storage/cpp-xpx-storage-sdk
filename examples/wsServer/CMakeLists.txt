cmake_minimum_required(VERSION 3.9)
project(server)

set(CMAKE_CXX_STANDARD 20)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

if(NOT WIN32)
    add_subdirectory(wsserver)
endif()

find_package(Boost 1.71.0 REQUIRED)

add_executable(serverApp 
    server.cpp 
    wsserver/Base64.cpp
    wsserver/Session.cpp 
    wsserver/Listener.cpp 
)

target_include_directories(serverApp PRIVATE ${Boost_INCLUDE_DIRS} wsserver)
target_link_libraries(serverApp PRIVATE ${Boost_LIBRARIES} pthread wsserver)