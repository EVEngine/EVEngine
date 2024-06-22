cmake_minimum_required(VERSION 3.21)
project(EVEngine)

set(EVENGINE_MAJOR_VERSION "0")
set(EVENGINE_MINOR_VERSION "1")
set(EVENGINE_PATCH_VERSION "0")

option(BUILD_TESTING "enable testing" ON)
message("Build UnitTest: " ${BUILD_TESTING})
if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "The build type (Debug, Release)")
endif()

include(CTest)


# This is the main function of the build script
function(main)
    get_available_build()
    load_third_party()
    set_include()
    check_ipo()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    add_subdirectory(platform/${BUILD_PLATFORM})
    add_subdirectory(src/engine)
    add_subdirectory(src/modules)
    add_subdirectory(src/scripts)

    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endfunction()


# This marco will create a module with the given name and folder
# It will scan the source code and create a xxx_src.txt file in the build folder
# When the build is triggered, it will check the real source code and see
# if there is any change. If there is, it will rescan the source code 

macro(create_module libname folder)
    execute_process(COMMAND ${CMAKE_COMMAND}
        -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}
        -Dmodule_name=${folder}
        -P ${CMAKE_SOURCE_DIR}/cmake/scan_source.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    file(READ ${CMAKE_CURRENT_BINARY_DIR}/${folder}_src.txt module_src)
    string(REPLACE "\n" ";" module_src "${module_src}")

    add_library(${libname} OBJECT ${module_src})

    add_custom_target(check_sources_${folder}
        ALL
        COMMAND ${CMAKE_COMMAND} 
        -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}
        -Dbuild_dir=${CMAKE_BINARY_DIR}
        -Dmodule_name=${folder} -P ${CMAKE_SOURCE_DIR}/cmake/rescan_source.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${folder}_src.txt)
    
    add_dependencies(${libname} third-party check_sources_${folder})
endmacro()

function(set_include)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts
        ${CMAKE_BINARY_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
    )
endfunction()

macro(get_available_build)
    set(AVAILABLE_PLATFORM "")

    if(UNIX)
        if(APPLE)
            list(APPEND AVAILABLE_PLATFORM "macosx")
            list(APPEND AVAILABLE_PLATFORM "ios")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "macosx" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        else() # Linux
            list(APPEND AVAILABLE_PLATFORM "linux")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "linux" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        endif()
    else()
        if(WIN32)
            list(APPEND AVAILABLE_PLATFORM "win32")
            list(APPEND AVAILABLE_PLATFORM "win_uwp")
            list(APPEND AVAILABLE_PLATFORM "android")
            set(BUILD_PLATFORM "win32" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})")
            MESSAGE("Build Platform:  ${BUILD_PLATFORM}")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        endif()
    endif()

    macro(set_platform_var name varname)
        string(COMPARE EQUAL "${BUILD_PLATFORM}" ${name} _cmp)

        if(_cmp)
            set(${varname} 1)
        endif()
    endmacro()

    set_platform_var(win32 EVENGINE_WINDOWS)
    set_platform_var(macosx EVENGINE_MACOSX)
    set_platform_var(linux EVENGINE_LINUX)
    set_platform_var(android EVENGINE_ANDROID)
    set_platform_var(ios EVENGINE_IOS)
    set_platform_var(win_uwp EVENGINE_WINDOWS_UWP)

    message("Availabe: ${AVAILABLE_PLATFORM}")

    if("${BUILD_PLATFORM}" IN_LIST AVAILABLE_PLATFORM)
        message("Build Platform:  ${BUILD_PLATFORM}")
    else()
        message(FATAL_ERROR "Unsupported Platform: ${BUILD_PLATFORM}")
    endif()
endmacro()

macro(findpkg name)
    find_package(${name} REQUIRED)

    if(${name}_FOUND)
        message(STATUS "Library ${name} ${${name}_VERSION} found")
    endif()
endmacro()

function(check_ipo)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT error)

    if(ipo_supported)
        message(STATUS "IPO / LTO enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO / LTO not supported: <${error}>")
    endif()
endfunction()

function(check_third_party_project name repo)
    include(ExternalProject)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${name}/CMakeLists.txt)
        # This is a git clone version
        message("load ${name} folder from git")
        set(GIT_HTTPS_LINK "")
    else()
        message("download ${name} source code")
        set(GIT_HTTPS_LINK ${repo})
    endif()

    ExternalProject_Add(${name}
        GIT_REPOSITORY ${GIT_HTTPS_LINK}
        GIT_TAG origin/main
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
        BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
        BUILD_COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}/ --target install --parallel 32
        BUILD_ALWAYS 0
        EXCLUDE_FROM_ALL 1
    )
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    link_directories(${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/lib)
endfunction()

function(load_third_party)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message("Build Type: Release")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message("Build Type: Debug")
        set(DEBUG_APPENDIX "-debug")
    else()
        message(FATAL_ERROR "Unsupported Build Type: ${CMAKE_BUILD_TYPE}")
    endif()

    set(TP_BUILD_PATH ${BUILD_PLATFORM}${DEBUG_APPENDIX})
    message("Third Party Build Path: ${TP_BUILD_PATH}")

    check_third_party_project(third-party "https://github.com/EVEngine/third-party")

    ADD_CUSTOM_TARGET(deps DEPENDS third-party)
endfunction()

main()