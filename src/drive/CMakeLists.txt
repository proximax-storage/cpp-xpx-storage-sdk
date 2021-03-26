cmake_minimum_required(VERSION 3.9)

storage_sdk_library_target(drive)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(drive PRIVATE
            /usr/local/lib/libtorrent-rasterbar.dylib.2.0.0
            /usr/local/opt/openssl@1.1/lib/libssl.dylib
            /usr/local/opt/openssl@1.1/lib/libcrypto.dylib
            "-framework CoreFoundation" "-framework SystemConfiguration")
else()
    target_link_libraries(drive torrent-rasterbar Threads::Threads)
endif()

include_directories(${PROJECT_SOURCE_DIR})
