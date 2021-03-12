cmake_minimum_required(VERSION 3.2)

include_directories(.)
include_directories(external)

include_directories(${PROJECT_SOURCE_DIR}/include)

#-----------------------------------------------------------------------------
# test action list
#-----------------------------------------------------------------------------

add_executable(testActionList testActionList.cpp ../src/ActionList.cpp)

target_link_libraries(testActionList
        ${Boost_LIBRARIES}
        catapult
        netio
        external)

#-----------------------------------------------------------------------------
# test drive
#-----------------------------------------------------------------------------
add_executable(testDrive testDrive.cpp ../src/Drive.cpp ../src/FsTree.cpp ../src/ActionList.cpp)

target_link_libraries(testDrive
       ${Boost_LIBRARIES}
       catapult
       netio
       external)

#-----------------------------------------------------------------------------
# test node connection
#-----------------------------------------------------------------------------

add_executable(testValidatorConnection testValidatorConnection.cpp)

target_link_libraries(testValidatorConnection
        PRIVATE
        ${Boost_LIBRARIES}
        catapult
        netio
        external
        )