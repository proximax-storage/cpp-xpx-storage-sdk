### set general cmake settings
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

### set boost settings
add_definitions(-DBOOST_ALL_DYN_LINK)
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

### set compiler settings
if(MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /EHsc")
        # in debug disable "potentially uninitialized local variable" (FP)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd /D_SCL_SECURE_NO_WARNINGS /wd4701")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MD /Zi")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")

        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG:FASTLINK")
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG")

        add_definitions(-D_WIN32_WINNT=0x0601 /w44287 /w44388)

        # explicitly disable linking against static boost libs
        add_definitions(-DBOOST_ALL_NO_LIB)

        # min/max macros are useless
        add_definitions(-DNOMINMAX)
        add_definitions(-DWIN32_LEAN_AND_MEAN)

        # mongo cxx view inherits std::iterator
        add_definitions(-D_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
        # boost asio associated_allocator
        add_definitions(-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        # -Wstrict-aliasing=1 perform most paranoid strict aliasing checks
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wstrict-aliasing=1 -Wnon-virtual-dtor -Wno-error=uninitialized -Wno-error=unknown-pragmas -Wno-unused-parameter")

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")

        # - Wno-maybe-uninitialized: false positives where gcc isn't sure if an uninitialized variable is used or not
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -Wno-maybe-uninitialized -g1 -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wno-maybe-uninitialized")

        # add memset_s
        add_definitions(-D_STDC_WANT_LIB_EXT1_=1)
        add_definitions(-D__STDC_WANT_LIB_EXT1__=1)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        # - Wno-c++98-compat*: catapult is not compatible with C++98
        # - Wno-disabled-macro-expansion: expansion of recursive macro is required for boost logging macros
        # - Wno-padded: allow compiler to automatically pad data types for alignment
        # - Wno-switch-enum: do not require enum switch statements to list every value (this setting is also incompatible with GCC warnings)
        # - Wno-weak-vtables: vtables are emitted in all translsation units for virtual classes with no out-of-line virtual method definitions
#	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
#		-Werror \
#		-fbracket-depth=1024 \
#		-Wno-c++98-compat \
#		-Wno-c++98-compat-pedantic \
#		-Wno-disabled-macro-expansion \
#		-Wno-padded \
#		-Wno-switch-enum \
#                -Wno-weak-vtables")

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
                -Wall\
                -Wextra\
                -Werror\
                -Werror-deprecated\
                -Wstrict-aliasing=1\
                -Wnon-virtual-dtor\
                -Wno-unused-const-variable\
                -Wno-unused-private-field\
                -Wno-unused-parameter
                ")
#            -Wno-error=uninitialized\

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")

        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -g1")
endif()

# set runpath for built binaries on linux
if(("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" AND "${CMAKE_SYSTEM_NAME}" MATCHES "Linux"))
        file(MAKE_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boost")
        set(CMAKE_SKIP_BUILD_RPATH FALSE)

        # $origin - to load plugins when running the server
        # $origin/boost - same, use our boost libs
        set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/deps${CMAKE_INSTALL_RPATH}")
        set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
        set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

        # use rpath for executables
        # (executable rpath will be used for loading indirect libs, this is needed because boost libs do not set runpath)
        # use newer runpath for shared libs
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--enable-new-dtags")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-new-dtags")
endif()

if(ARCHITECTURE_NAME)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${ARCHITECTURE_NAME}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=${ARCHITECTURE_NAME}")
endif()

### define target helper functions

# used to define a catapult target (library, executable) and automatically enables PCH for clang
function(storage_sdk_target TARGET_NAME)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)

        # indicate boost as a dependency
        target_link_libraries(${TARGET_NAME} ${Boost_LIBRARIES} ${CMAKE_DL_LIBS})

        # copy boost shared libraries
        foreach(BOOST_COMPONENT ATOMIC SYSTEM DATE_TIME REGEX TIMER CHRONO LOG THREAD FILESYSTEM) # PROGRAM_OPTIONS STACKTRACE_BACKTRACE)
                if(MSVC)
                        # copy into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)
                        string(REPLACE ".lib" ".dll" BOOSTDLLNAME ${Boost_${BOOST_COMPONENT}_LIBRARY_RELEASE})
                        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${BOOSTDLLNAME}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)")
                elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
                        # copy into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boost
                        set(BOOSTDLLNAME ${Boost_${BOOST_COMPONENT}_LIBRARY_RELEASE})
                        set(BOOSTVERSION "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}")
                        get_filename_component(BOOSTFILENAME ${BOOSTDLLNAME} NAME)
                        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${BOOSTDLLNAME}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boost")
                endif()
        endforeach()
endfunction()

# finds all files comprising a target
function(storage_sdk_find_all_target_files TARGET_TYPE TARGET_NAME)
        if (CMAKE_VERBOSE_MAKEFILE)
                message("processing ${TARGET_TYPE} '${TARGET_NAME}'")
        endif()

        file(GLOB ${TARGET_NAME}_INCLUDE_SRC "*.h")
        file(GLOB ${TARGET_NAME}_SRC "*.cpp")

        set(CURRENT_FILES ${${TARGET_NAME}_INCLUDE_SRC} ${${TARGET_NAME}_SRC})
        SOURCE_GROUP("src" FILES ${CURRENT_FILES})
        set(TARGET_FILES ${CURRENT_FILES})

        # add any (optional) subdirectories
        foreach(arg ${ARGN})
                set(SUBDIR ${arg})
                if (CMAKE_VERBOSE_MAKEFILE)
                        message("+ processing subdirectory '${arg}'")
                endif()

                file(GLOB ${TARGET_NAME}_${SUBDIR}_INCLUDE_SRC "${SUBDIR}/*.h")
                file(GLOB ${TARGET_NAME}_${SUBDIR}_SRC "${SUBDIR}/*.cpp")

                set(CURRENT_FILES ${${TARGET_NAME}_${SUBDIR}_INCLUDE_SRC} ${${TARGET_NAME}_${SUBDIR}_SRC})
                SOURCE_GROUP("${SUBDIR}" FILES ${CURRENT_FILES})
                set(TARGET_FILES ${TARGET_FILES} ${CURRENT_FILES})
        endforeach()

        set(${TARGET_NAME}_FILES ${TARGET_FILES} PARENT_SCOPE)
endfunction()

# used to define a storage sdk object library
function(storage_sdk_object_library TARGET_NAME)
        add_library(${TARGET_NAME} OBJECT ${ARGN})
        set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
endfunction()

# used to define a catapult library, creating an appropriate source group and adding a library
function(storage_sdk_library TARGET_NAME)
        storage_sdk_find_all_target_files("lib" ${TARGET_NAME} ${ARGN})
        add_library(${TARGET_NAME} ${${TARGET_NAME}_FILES})
endfunction()

# combines storage_sdk_library and storage_sdk_target
function(storage_sdk_library_target TARGET_NAME)
        storage_sdk_library(${TARGET_NAME} ${ARGN})
        set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
        storage_sdk_target(${TARGET_NAME})
endfunction()

# used to define a storage sdk shared library, creating an appropriate source group and adding a library
function(storage_sdk_shared_library TARGET_NAME)
        storage_sdk_find_all_target_files("shared lib" ${TARGET_NAME} ${ARGN})

        add_definitions(-DDLL_EXPORTS)

        if(MSVC)
                set_win_version_definitions(${TARGET_NAME} VFT_DLL)
        endif()

        add_library(${TARGET_NAME} SHARED ${${TARGET_NAME}_FILES} ${VERSION_RESOURCES})
endfunction()

# combines storage_sdk_shared_library and storage_sdk_target
function(storage_sdk_shared_library_target TARGET_NAME)
        storage_sdk_shared_library(${TARGET_NAME} ${ARGN})
        storage_sdk_target(${TARGET_NAME})
endfunction()

# used to define a catapult executable, creating an appropriate source group and adding an executable
function(storage_sdk_executable TARGET_NAME)
        storage_sdk_find_all_target_files("exe" ${TARGET_NAME} ${ARGN})

        if(MSVC)
                set_win_version_definitions(${TARGET_NAME} VFT_APP)
        endif()

        add_executable(${TARGET_NAME} ${${TARGET_NAME}_FILES} ${VERSION_RESOURCES})

        if(WIN32 AND MINGW)
                target_link_libraries(${TARGET_NAME} wsock32 ws2_32)
        endif()
endfunction()

# used to define a catapult header only target, creating an appropriate source group in order to allow VS to create an appropriate folder
function(storage_sdk_header_only_target TARGET_NAME)
        if(MSVC)
                storage_sdk_find_all_target_files("hdr" ${TARGET_NAME} ${ARGN})

                if (CMAKE_VERBOSE_MAKEFILE)
                        foreach(arg ${ARGN})
                                message("adding subdirectory '${arg}'")
                        endforeach()
                endif()

                # https://stackoverflow.com/questions/39887352/how-to-create-a-cmake-header-only-library-that-depends-on-external-header-files
                # target_sources doesn't work with interface libraries, but we can use custom_target (with empty action)
                add_custom_target(${TARGET_NAME} SOURCES ${${TARGET_NAME}_FILES})
        endif()
endfunction()
