cmake_minimum_required(VERSION 3.9)

add_definitions(-DSIRIUS_DRIVE_MULTI)

include_directories(${PROJECT_SOURCE_DIR}/include)

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/libtorrent/include)

### test node connection
#@#add_executable(testValidatorConnection testValidatorConnection.cpp common/ConnnectionAdapter.cpp)
#@#target_link_libraries(testValidatorConnection connection crypto ionet net model)

### test discovery using single node
#@#add_executable(testSingleNodeDiscovery testSingleNodeDiscovery.cpp common/ConnnectionAdapter.cpp)
#@#target_link_libraries(testSingleNodeDiscovery connection crypto ionet net model nodediscovery)


### download flat file example
#add_subdirectory(downloadFileFlat)

### modify flat drive example
add_subdirectory(modifyDriveFlat3)
add_subdirectory(modifyDriveFlat4)
add_subdirectory(standalone-tests)
add_subdirectory(extensionEmulator)
#add_subdirectory(modifyDriveFlat2)
add_subdirectory(replicator2)
add_subdirectory(client2)
#add_subdirectory(sendMessage)

