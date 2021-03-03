cmake_minimum_required(VERSION 3.18)
project(cpp-xpx-storage-sdk VERSION 1.0.0)


#-----------------------------------------------------------------------------
# options and features
#-----------------------------------------------------------------------------

option(XPX_CHAIN_SDK_BUILD_STATIC "Build static version of library." OFF)
option(XPX_CHAIN_SDK_BUILD_TESTS "Build tests." OFF)
option(XPX_CHAIN_SDK_BUILD_EXAMPLES "Build examples." OFF)

#if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
#	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "XPX-CHAIN SDK build type." FORCE)
#	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
#		"Debug" "Release" "RelWithDebInfo" "MinSizeRel")
#endif()

if (MSVC)
	add_compile_options(/W3)
else()
	add_compile_options(-Wall)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CheckFeatures.cmake)
check_string_literal_operator_template(HAVE_STRING_LITERAL_OPERATOR_TEMPLATE)

#-----------------------------------------------------------------------------
# sources
#-----------------------------------------------------------------------------

file(GLOB_RECURSE sources
    "include/*.h"
    "include/FileTransmitter/*.h"
    "src/*.cpp")

include_directories( SYSTEM "cereal/include" )
include_directories( PRIVATE "include" )

#if (MSVC)
	# disable silly warning 'integral constant overflow' which is not easily fixed for 64bit types in constexpr
#	set_source_files_properties(${sources} PROPERTIES
#		COMPILE_FLAGS "/wd4307")

	# disable warnings from third-party sources
#	set_source_files_properties(${ref10_sources} ${ripemd160_sources} ${sha3_sources} ${sha256_sources} PROPERTIES
#		COMPILE_FLAGS "/wd4244 /wd4146")
#else()
	# disable clang warning about using non-standard string literal operator template (N3599 proposal)
#	if (HAVE_STRING_LITERAL_OPERATOR_TEMPLATE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#		set_source_files_properties(${sources} PROPERTIES
#			COMPILE_FLAGS "-Wno-gnu-string-literal-operator-template")
#	endif()

	# disable all warnings from third-party sources
#	set_source_files_properties(${ref10_sources} ${ripemd160_sources} ${sha3_sources} ${sha256_sources} PROPERTIES
#		COMPILE_FLAGS "-w")
#endif()

#list(APPEND sources ${ref10_sources} ${ripemd160_sources} ${sha3_sources} ${sha256_sources})
#source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${sources})

#-----------------------------------------------------------------------------
# targets
#-----------------------------------------------------------------------------

#if (XPX_CHAIN_SDK_BUILD_STATIC)
#        add_library(xpxstoragesdk STATIC "")
#        set_target_properties(xpxstoragesdk PROPERTIES
#                OUTPUT_NAME "xpxstoragesdk-static")
#else()

        add_library(xpxstoragesdk SHARED "src/ActionList.cpp")

        set_target_properties(xpxstoragesdk PROPERTIES
		VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR})

	if (MSVC)
                set_target_properties(xpxstoragesdk PROPERTIES
			WINDOWS_EXPORT_ALL_SYMBOLS ON)
	endif()
#endif()

#target_sources(xpxstoragesdk PRIVATE ${sources})

#target_compile_definitions(xpxstoragesdk PRIVATE
#	XPX_CHAIN_USE_STRING_LITERAL_OPERATOR_TEMPLATE=${HAVE_STRING_LITERAL_OPERATOR_TEMPLATE})

#target_include_directories(xpxstoragesdk
#	PUBLIC
#		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
#	PRIVATE
#		${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(xpxstoragesdk PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(xpxstoragesdk)

#-----------------------------------------------------------------------------
# tests
#-----------------------------------------------------------------------------

#if (XPX_CHAIN_SDK_BUILD_TESTS)
#	include(CTest)
#	enable_testing()
#	add_subdirectory(tests)
#endif()

#if (XPX_CHAIN_SDK_BUILD_EXAMPLES)
#	add_executable(get_block ${CMAKE_CURRENT_SOURCE_DIR}/examples/get_block.cpp)
#	target_link_libraries(get_block
#                        xpxstoragesdk
#			CONAN_PKG::rapidjson
#			CONAN_PKG::boost
#			CONAN_PKG::OpenSSL)
#endif()

add_subdirectory(examples)
