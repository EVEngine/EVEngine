cmake_minimum_required(VERSION 3.16)
project(EVEngine)

set(EVENGINE_MAJOR_VERSION "0")
set(EVENGINE_MINOR_VERSION "1")
set(EVENGINE_PATCH_VERSION "0")

option(BUILD_TESTING "" OFF)
include(CTest)

function(main)
    get_available_build()
    load_third_party()
    set_include()
    set_library()
    set(CMAKE_CXX_STANDARD 17)

    add_subdirectory(platform/${BUILD_PLATFORM})
    add_subdirectory(src/engine)
    add_subdirectory(src/modules)
    add_subdirectory(src/scripts)

    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endfunction()

macro(create_module libname folder)
    file(GLOB_RECURSE module_src ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp)
    file(GLOB_RECURSE module_sub_src ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/**/*.cpp)
    add_library(${libname} ${module_src} ${module_sub_src})
endmacro()

function(set_include)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts
        ${CMAKE_BINARY_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party-binary/include
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
    )
endfunction()

function(set_library)
    link_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party-binary/lib
    )
endfunction()

macro(get_available_build)
    set(AVAILABLE_PLATFORM "")
    if (UNIX)
        if (APPLE)
            list(APPEND AVAILABLE_PLATFORM "macosx")
            list(APPEND AVAILABLE_PLATFORM "ios")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "macosx" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})" )
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM}) 
        else() # Linux
            list(APPEND AVAILABLE_PLATFORM "linux")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "linux" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})" )
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM}) 
        endif()
    else()
        if (WIN32)
            list(APPEND AVAILABLE_PLATFORM "win32")
            list(APPEND AVAILABLE_PLATFORM "win32_uwp")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "win32" "The build platfrom (${AVAILABLE_PLATFORM})" )
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM}) 
        endif()
    endif()

    macro(set_platform_var name varname)
        string(COMPARE EQUAL "${BUILD_PLATFORM}" ${name} _cmp)
        if (_cmp) 
            set(${varname} 1) 
        endif()
    endmacro()

    set_platform_var(win32     EVENGINE_WINDOWS)
    set_platform_var(macosx    EVENGINE_MACOSX)
    set_platform_var(linux     EVENGINE_LINUX)
    set_platform_var(android   EVENGINE_ANDROID)
    set_platform_var(ios       EVENGINE_IOS)
    set_platform_var(win32_uwp EVENGINE_WINDOWS_UWP)

    message("Availabe: ${AVAILABLE_PLATFORM}")
    if ("${BUILD_PLATFORM}" IN_LIST AVAILABLE_PLATFORM)
        message("Build Platform:  ${BUILD_PLATFORM}")
    else() 
        message( FATAL_ERROR "Unsupported Platform: ${BUILD_PLATFORM}" )
    endif()
endmacro()


macro(findpkg name)
    find_package(${name} REQUIRED)
    if (${name}_FOUND)
        message(STATUS "Library ${name} ${${name}_VERSION} found")
    endif()
endmacro()


function(load_third_party)
    include(ExternalProject)
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third-party/CMakeLists.txt)
        # This is a git clone version
        message("load third-party folder from git")
        set(GIT_HTTPS_LINK "")
    else() 
        message("download third-party source code")
        set(GIT_HTTPS_LINK https://github.com/EVEngine/third-party)
    endif()
    ExternalProject_Add(third-party
        GIT_REPOSITORY   ${GIT_HTTPS_LINK}
        SOURCE_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/third-party
        BINARY_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/third-party/build
        CMAKE_ARGS       -DCMAKE_BUILD_TYPE=Release
        BUILD_COMMAND    ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/third-party/build/ --target install --parallel 16
        BUILD_ALWAYS     1
        EXCLUDE_FROM_ALL 1
    )

    ExternalProject_Add(third-party-debug
        GIT_REPOSITORY   ${GIT_HTTPS_LINK}
        SOURCE_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/third-party
        BINARY_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-debug
        CMAKE_ARGS       -DCMAKE_BUILD_TYPE=Debug
        BUILD_COMMAND    ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/third-party/build-debug/ --target install --parallel 16
        BUILD_ALWAYS     1
        EXCLUDE_FROM_ALL 1
    )
    ADD_CUSTOM_TARGET(deps DEPENDS third-party)
    ADD_CUSTOM_TARGET(deps-d DEPENDS third-party-debug)

    set(SDL2_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third-party-binary)
    findpkg(SDL2)
endfunction()


main()