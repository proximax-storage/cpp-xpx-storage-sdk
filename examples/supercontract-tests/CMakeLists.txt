cmake_minimum_required(VERSION 3.2)
project(standalone-tests)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED)
find_package(GTest REQUIRED) # Find the google testing framework on your system

file(GLOB TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test*.cpp")

add_executable(supercontract-tests utils.h utils.cpp TestEnvironment.h ${TEST_FILES} testSupercontractRemoveNonExistingDir.cpp)

target_include_directories(supercontract-tests PUBLIC ${GTEST_INCLUDE_DIRS})

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D_GNU_SOURCE)
endif()

enable_testing()
add_test(supercontract-tests "./supercontract-tests")
target_link_libraries(supercontract-tests drive storage-crypto torrent-sirius Threads::Threads ${GTEST_LIBRARIES} GTest::gtest_main)

