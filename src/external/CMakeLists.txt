cmake_minimum_required(VERSION 3.9)
project(sdk_external)

if(WIN32)
	set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
	set(BUILD_SHARED_LIBS TRUE)
endif()

file(GLOB external_ref10_SRC
	"ref10/*.cpp"
	"ref10/*.h")
file(GLOB external_sha256_SRC
	"sha256/*.cpp"
	"sha256/*.h")
file(GLOB external_sha3_SRC
	"sha3/*.cpp"
	"sha3/*.h")
file(GLOB external_ripemd160_SRC
	"ripemd160/*.cpp"
	"ripemd160/*.h"
)

SOURCE_GROUP("ref10" FILES ${external_ref10_SRC})
SOURCE_GROUP("sha256" FILES ${external_sha256_SRC})
SOURCE_GROUP("sha3" FILES ${external_sha3_SRC})
SOURCE_GROUP("ripemd160" FILES ${external_ripemd160_SRC})

add_definitions(-w)
add_library(sdk_external SHARED ${external_ref10_SRC} ${external_sha256_SRC} ${external_sha3_SRC} ${external_ripemd160_SRC})
