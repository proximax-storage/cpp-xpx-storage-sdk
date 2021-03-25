cmake_minimum_required(VERSION 3.9)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_subdirectory(download)
add_subdirectory(upload)
add_subdirectory(libtorrent)

add_subdirectory(libtorrenttest)

### test action list
add_executable(testActionList testActionList.cpp)
target_link_libraries(testActionList drive)

### test drive
add_executable(testDrive testDrive.cpp)
target_link_libraries(testDrive drive)

### test node connection
add_executable(testValidatorConnection testValidatorConnection.cpp)
target_link_libraries(testValidatorConnection connection crypto ionet net)

