cmake_minimum_required(VERSION 3.9)
find_package(OpenSSL REQUIRED)

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
add_subdirectory(wsserver)

# find_package(Boost 1.71.0 REQUIRED)
set(CMAKE_CXX_STANDARD 20)

# add_executable(serverApp 
#     main.cpp 
#     wsserver/Base64.cpp
#     wsserver/Session.cpp 
#     wsserver/Listener.cpp 
# )

# target_include_directories(serverApp 
#    PRIVATE 
#    ${Boost_INCLUDE_DIRS} 
#    ${OPENSSL_INCLUDE_DIR} 
#    wsserver
# )
# target_link_libraries(serverApp 
#    PRIVATE 
#    ${Boost_LIBRARIES} 
#    ${OPENSSL_LIBRARIES} 
#    pthread 
#    wsserver
# )

#end
if (XPX_STORAGE_SDK_BUILD_EMULATOR)
   add_subdirectory(emulator)
endif()
