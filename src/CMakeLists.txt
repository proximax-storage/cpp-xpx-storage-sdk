cmake_minimum_required(VERSION 3.10)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

add_subdirectory(api)
add_subdirectory(drive)
add_subdirectory(external)
add_subdirectory(connection)
add_subdirectory(crypto)
add_subdirectory(ionet)
add_subdirectory(model)
add_subdirectory(net)
add_subdirectory(utils)
add_subdirectory(nodediscovery)
add_subdirectory(easyloggingpp)

if (XPX_STORAGE_SDK_BUILD_EMULATOR)
   add_subdirectory(emulator)
endif()
