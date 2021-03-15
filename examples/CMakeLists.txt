cmake_minimum_required(VERSION 3.2)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(testActionList testActionList.cpp ../src/ActionList.cpp)
add_executable(testDrive testDrive.cpp ../src/Drive.cpp ../src/FsTree.cpp  ../src/ActionList.cpp)

add_subdirectory(download)
add_subdirectory(upload)

add_subdirectory(libtorrenttest)
