#cmake_minimum_required(VERSION 3.9)
add_definitions(-DSIRIUS_DRIVE_MULTI)
set(CMAKE_CXX_STANDARD 20)

storage_sdk_shared_library_target(emulator)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(drive
            #${CMAKE_SOURCE_DIR}/libtorrent/_build/Debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/libtorrent/cmake-build-debug/libtorrent-sirius.dylib
            ${CMAKE_SOURCE_DIR}/rpclib/build/librpc.a
            crypto
            )
#            "-framework CoreFoundation" "-framework SystemConfiguration")
else()
	target_link_libraries(emulator drive)

    if (XPX_STORAGE_SDK_BUILD_EMULATOR)
        target_link_libraries(emulator xpx.rpc)
    endif()
endif()
