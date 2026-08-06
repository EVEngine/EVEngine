cmake_minimum_required(VERSION 3.21)
project(EVEngine)

set(EVENGINE_MAJOR_VERSION "0")
set(EVENGINE_MINOR_VERSION "1")
set(EVENGINE_PATCH_VERSION "0")

option(BUILD_TESTING "enable testing" ON)
option(EVENGINE_BUILD_DEMO "Build optional Demo module (procedural demo assets + default demo.nut)" ON)
message("Build UnitTest: " ${BUILD_TESTING})
message("Build Demo module: " ${EVENGINE_BUILD_DEMO})
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
    # cl.exe gets STL/UCRT from %INCLUDE% (vcvars); CMake omits those from compile_commands.json.
    # Export them so clangd can resolve <cstdint> etc. Configure via cmake/with-msvc.cmd.
    if(MSVC AND DEFINED ENV{INCLUDE} AND NOT "$ENV{INCLUDE}" STREQUAL "")
        set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES $ENV{INCLUDE})
        set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES $ENV{INCLUDE})
        message(STATUS "Exported MSVC INCLUDE paths into compile_commands for clangd")
    elseif(MSVC)
        message(STATUS "MSVC INCLUDE unset; clangd may not find STL headers. Use cmake/with-msvc.cmd")
    endif()

    # zeroerr before modules so Debug ASSERT headers/defs are available when compiling engine code.
    set(BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(USE_MOLD OFF CACHE BOOL "" FORCE)
    set(COLORFUL_OUTPUT OFF CACHE STRING "" FORCE)
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
        message(STATUS "Android build: unit tests disabled")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
        message(STATUS "iOS build: unit tests disabled")
    endif()
    add_subdirectory(external/zeroerr)
    target_compile_definitions(zeroerr PUBLIC ZEROERR_NO_MAIN)
    # Strip ASSERT macros in non-Debug (upstream ZEROERR_NO_ASSERT under NDEBUG is commented out).
    add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:ZEROERR_NO_ASSERT>)

    # Platform CMake may set EVENGINE_PLATFORM_LIB; clear stale cache from other platforms.
    unset(EVENGINE_PLATFORM_LIB CACHE)
    add_subdirectory(platform/${BUILD_PLATFORM})
    add_subdirectory(src/engine)
    add_subdirectory(src/modules)
    add_subdirectory(src/scripts)

    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()

    include(${CMAKE_SOURCE_DIR}/cmake/install_sdk.cmake)
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

    # Xcode + OBJECT libraries often emit hashed object filenames that the
    # dependent link phase still references without the hash (duplicate basenames
    # like Window.cpp). Use STATIC archives under Xcode to keep linking stable.
    if(CMAKE_GENERATOR MATCHES "Xcode")
        add_library(${libname} STATIC ${module_src})
    else()
        add_library(${libname} OBJECT ${module_src})
    endif()

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


# This function will set the include directories for the project
function(set_include)
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts
        ${CMAKE_BINARY_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        # sunxfancy/ECS.hpp (git submodule) + zeroerr headers
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/zeroerr/include
    )

    # Prefer system Vulkan SDK; otherwise use third-party headers + local import lib.
    # On Apple desktop, MoltenVK from the LunarG Vulkan SDK is required.
    # On iOS, link/embed the SDK's MoltenVK.xcframework (iphoneos arm64 slice).
    # On Android, link the NDK loader (libvulkan.so).
    unset(Vulkan_INCLUDE_DIR CACHE)
    unset(Vulkan_LIBRARY CACHE)
    find_package(Vulkan QUIET)
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        find_library(ANDROID_VULKAN_LIB vulkan)
        if(NOT ANDROID_VULKAN_LIB)
            message(FATAL_ERROR "Android NDK libvulkan not found (need ANDROID_PLATFORM >= 24)")
        endif()
        if(Vulkan_INCLUDE_DIRS)
            include_directories(${Vulkan_INCLUDE_DIRS})
        else()
            include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        endif()
        set(EVENGINE_VULKAN_LIB ${ANDROID_VULKAN_LIB} CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan (Android): ${ANDROID_VULKAN_LIB}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # Resolve MoltenVK.xcframework from VULKAN_SDK / common install locations.
        set(_eve_moltenvk_xcframework "")
        if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
            foreach(_cand
                "$ENV{VULKAN_SDK}/../iOS/lib/MoltenVK.xcframework"
                "$ENV{VULKAN_SDK}/iOS/lib/MoltenVK.xcframework"
                "$ENV{VULKAN_SDK}/MoltenVK.xcframework")
                get_filename_component(_abs "${_cand}" ABSOLUTE)
                if(EXISTS "${_abs}")
                    set(_eve_moltenvk_xcframework "${_abs}")
                    break()
                endif()
            endforeach()
        endif()
        if(NOT _eve_moltenvk_xcframework)
            file(GLOB _eve_vk_ios_glob "$ENV{HOME}/VulkanSDK/*/iOS/lib/MoltenVK.xcframework")
            if(_eve_vk_ios_glob)
                list(GET _eve_vk_ios_glob 0 _eve_moltenvk_xcframework)
            endif()
        endif()
        if(NOT _eve_moltenvk_xcframework OR NOT EXISTS "${_eve_moltenvk_xcframework}")
            message(FATAL_ERROR
                "MoltenVK.xcframework not found for iOS.\n"
                "  Install the LunarG Vulkan SDK and ensure iOS/lib/MoltenVK.xcframework exists,\n"
                "  or set VULKAN_SDK to the SDK root (macOS or iOS tree).")
        endif()
        set(EVENGINE_MOLTENVK_XCFRAMEWORK "${_eve_moltenvk_xcframework}" CACHE INTERNAL "MoltenVK xcframework")
        set(EVENGINE_MOLTENVK_FRAMEWORK_DIR
            "${_eve_moltenvk_xcframework}/ios-arm64"
            CACHE INTERNAL "MoltenVK framework search path for iphoneos")
        if(Vulkan_INCLUDE_DIRS)
            include_directories(${Vulkan_INCLUDE_DIRS})
        else()
            include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        endif()
        # Placeholder; engine CMake links -F + -framework MoltenVK explicitly.
        set(EVENGINE_VULKAN_LIB "" CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan (iOS MoltenVK): ${EVENGINE_MOLTENVK_XCFRAMEWORK}")
    elseif(Vulkan_FOUND AND Vulkan_INCLUDE_DIRS AND Vulkan_LIBRARIES)
        message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")
        include_directories(${Vulkan_INCLUDE_DIRS})
        set(EVENGINE_VULKAN_LIB Vulkan::Vulkan CACHE INTERNAL "Vulkan link target")
    elseif(APPLE)
        message(FATAL_ERROR
            "Vulkan SDK with MoltenVK is required on macOS.\n"
            "  1. Install the LunarG Vulkan SDK for macOS from https://vulkan.lunarg.com/\n"
            "  2. Source the SDK env:  source ~/VulkanSDK/<version>/setup-env.sh\n"
            "  3. Re-run CMake (Vulkan_LIBRARY should resolve to libvulkan / MoltenVK).\n"
            "Current hints: VULKAN_SDK='$ENV{VULKAN_SDK}'")
    else()
        unset(Vulkan_INCLUDE_DIR CACHE)
        unset(Vulkan_LIBRARY CACHE)
        set(_eve_vk_inc ${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        set(_eve_vk_lib ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vulkan/vulkan-1.lib)
        include_directories(${_eve_vk_inc})
        set(EVENGINE_VULKAN_LIB ${_eve_vk_lib} CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan: using ${_eve_vk_inc} + ${_eve_vk_lib}")
    endif()
endfunction()


#  Get available build based on the platform you are using
macro(get_available_build)
    set(AVAILABLE_PLATFORM "")

    # Cross-compiling for Android (NDK toolchain) must win over host Apple/Linux detection.
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        list(APPEND AVAILABLE_PLATFORM "android")
        set(BUILD_PLATFORM "android" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})")
        SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND AVAILABLE_PLATFORM "ios")
        set(BUILD_PLATFORM "ios" CACHE STRING "The build platfrom (${AVAILABLE_PLATFORM})")
        SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
    elseif(UNIX)
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

    # Clear previous platform defines so configure_file / #cmakedefine stay accurate
    # when switching BUILD_PLATFORM in the same build tree.
    foreach(_eve_plat_var IN ITEMS
            EVENGINE_WINDOWS EVENGINE_MACOSX EVENGINE_LINUX
            EVENGINE_ANDROID EVENGINE_IOS EVENGINE_WINDOWS_UWP)
        unset(${_eve_plat_var} CACHE)
        unset(${_eve_plat_var})
    endforeach()

    macro(set_platform_var name varname)
        string(COMPARE EQUAL "${BUILD_PLATFORM}" ${name} _cmp)

        if(_cmp)
            # CACHE INTERNAL so values survive function(main) and reach configure_file.
            set(${varname} 1 CACHE INTERNAL "EVEngine platform define")
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


# This macro will find the package and print out the version
macro(findpkg name)
    find_package(${name} REQUIRED)

    if(${name}_FOUND)
        message(STATUS "Library ${name} ${${name}_VERSION} found")
    endif()
endmacro()

# This function will check if the compiler support IPO and LTO
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


# This function will check the third party project
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

    set(_eve_tp_cmake_args
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
    )
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DANDROID_ABI=${ANDROID_ABI}
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DANDROID_STL=${ANDROID_STL}
            -DANDROID_NDK=${ANDROID_NDK}
            -DCMAKE_SYSTEM_NAME=Android
        )
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND _eve_tp_cmake_args -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
        endif()
        message(STATUS "third-party Android toolchain: ABI=${ANDROID_ABI} PLATFORM=${ANDROID_PLATFORM}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        find_program(_eve_ninja ninja REQUIRED)
        set(_eve_ios_tp_toolchain "${CMAKE_SOURCE_DIR}/cmake/ios.toolchain.cmake")
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${_eve_ios_tp_toolchain}
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
            -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
            -DIOS=TRUE
            -DVIDEO_COCOA=OFF
            -DVIDEO_OPENGL=OFF
            -DCMAKE_MAKE_PROGRAM=${_eve_ninja}
        )
        # Force Ninja for deps: parent Xcode generator makes SDL try_compile unbearably slow.
        set(_eve_tp_use_ninja TRUE)
        message(STATUS "third-party iOS toolchain: ARCH=${CMAKE_OSX_ARCHITECTURES} DEPLOY=${CMAKE_OSX_DEPLOYMENT_TARGET} GEN=Ninja FILE=${_eve_ios_tp_toolchain}")
    endif()

    set(_eve_tp_build_cmd ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}/ --target install --parallel 8)

    if(_eve_tp_use_ninja)
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG origin/main
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            CMAKE_GENERATOR "Ninja"
            CMAKE_ARGS ${_eve_tp_cmake_args}
            BUILD_COMMAND ${_eve_tp_build_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    else()
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG origin/main
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            CMAKE_ARGS ${_eve_tp_cmake_args}
            BUILD_COMMAND ${_eve_tp_build_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    endif()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    link_directories(${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/lib)
endfunction()

# Load third party project from github
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