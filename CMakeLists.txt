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
    # Chinese Windows (code page 936): source UTF-8 comments/strings otherwise trigger C4819.
    if(MSVC)
        add_compile_options(/utf-8)
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

    include(${CMAKE_SOURCE_DIR}/cmake/timidity_share.cmake)
    eve_stage_timidity_share()

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
        # sunxfancy/ECS.hpp + ik.hpp (git submodules) + zeroerr headers
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ik.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/external/zeroerr/include
        # Dungeon Template Library (header-only)
        ${CMAKE_CURRENT_SOURCE_DIR}/external/DungeonTemplateLibrary/include
    )

    # Prefer system Vulkan SDK (find_package). No vendored vulkan-1.lib fallback.
    # On Apple desktop, MoltenVK from the LunarG Vulkan SDK is required.
    # On iOS, link/embed the SDK's MoltenVK.xcframework (iphoneos arm64 slice).
    # On Android, link the NDK loader (libvulkan.so).
    #
    # Windows: installer sets Machine-level VULKAN_SDK, but long-lived IDEs (Cursor)
    # often still lack it in the process env. Probe C:/VulkanSDK/* when unset.
    if(WIN32 AND (NOT DEFINED ENV{VULKAN_SDK} OR "$ENV{VULKAN_SDK}" STREQUAL ""))
        file(GLOB _eve_vk_sdk_dirs "C:/VulkanSDK/*")
        list(SORT _eve_vk_sdk_dirs COMPARE NATURAL ORDER DESCENDING)
        foreach(_eve_vk_sdk IN LISTS _eve_vk_sdk_dirs)
            if(EXISTS "${_eve_vk_sdk}/Include/vulkan/vulkan.h"
               AND (EXISTS "${_eve_vk_sdk}/Lib/vulkan-1.lib"
                    OR EXISTS "${_eve_vk_sdk}/Lib/vulkan.lib"))
                set(ENV{VULKAN_SDK} "${_eve_vk_sdk}")
                message(STATUS "Vulkan: VULKAN_SDK unset in this process; using ${_eve_vk_sdk}")
                break()
            endif()
        endforeach()
        unset(_eve_vk_sdk_dirs)
        unset(_eve_vk_sdk)
    endif()
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
        # Resolve MoltenVK for iphoneos arm64.
        # LunarG ships two different xcframeworks:
        #   iOS/lib/.../ios-arm64/MoltenVK.framework  (dynamic; preferred, embeddable)
        #   macOS/lib/.../ios-arm64/libMoltenVK.a      (static; CI zip often only has this)
        # Prefer the framework; fall back to the static archive.
        set(_eve_mvk_candidates "")
        if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
            list(APPEND _eve_mvk_candidates
                "$ENV{VULKAN_SDK}/../iOS/lib/MoltenVK.xcframework"
                "$ENV{VULKAN_SDK}/iOS/lib/MoltenVK.xcframework"
                "$ENV{VULKAN_SDK}/lib/MoltenVK.xcframework"
                "$ENV{VULKAN_SDK}/MoltenVK.xcframework")
        endif()
        file(GLOB _eve_vk_ios_glob
            "$ENV{HOME}/VulkanSDK/*/iOS/lib/MoltenVK.xcframework"
            "$ENV{HOME}/VulkanSDK/*/macOS/lib/MoltenVK.xcframework")
        list(APPEND _eve_mvk_candidates ${_eve_vk_ios_glob})

        set(_eve_moltenvk_xcframework "")
        set(_eve_moltenvk_link_mode "")
        set(_eve_moltenvk_framework_dir "")
        set(_eve_moltenvk_static_lib "")
        foreach(_cand IN LISTS _eve_mvk_candidates)
            get_filename_component(_abs "${_cand}" ABSOLUTE)
            if(NOT EXISTS "${_abs}")
                continue()
            endif()
            set(_slice "${_abs}/ios-arm64")
            if(EXISTS "${_slice}/MoltenVK.framework")
                set(_eve_moltenvk_xcframework "${_abs}")
                set(_eve_moltenvk_link_mode "framework")
                set(_eve_moltenvk_framework_dir "${_slice}")
                break()
            endif()
            if(NOT _eve_moltenvk_xcframework AND EXISTS "${_slice}/libMoltenVK.a")
                set(_eve_moltenvk_xcframework "${_abs}")
                set(_eve_moltenvk_link_mode "static")
                set(_eve_moltenvk_static_lib "${_slice}/libMoltenVK.a")
                # Keep scanning: a later candidate may still provide the framework.
            endif()
        endforeach()

        if(NOT _eve_moltenvk_link_mode)
            message(FATAL_ERROR
                "MoltenVK ios-arm64 not found (need MoltenVK.framework or libMoltenVK.a).\n"
                "  Install the LunarG Vulkan SDK (full tree with iOS/lib preferred),\n"
                "  or set VULKAN_SDK to the SDK macOS/ or iOS/ root.\n"
                "  Current VULKAN_SDK='$ENV{VULKAN_SDK}'")
        endif()

        set(EVENGINE_MOLTENVK_XCFRAMEWORK "${_eve_moltenvk_xcframework}" CACHE INTERNAL "MoltenVK xcframework")
        set(EVENGINE_MOLTENVK_LINK_MODE "${_eve_moltenvk_link_mode}" CACHE INTERNAL "MoltenVK link mode: framework|static")
        set(EVENGINE_MOLTENVK_FRAMEWORK_DIR "${_eve_moltenvk_framework_dir}" CACHE INTERNAL "MoltenVK framework search path")
        set(EVENGINE_MOLTENVK_STATIC_LIB "${_eve_moltenvk_static_lib}" CACHE INTERNAL "MoltenVK static archive")
        if(Vulkan_INCLUDE_DIRS)
            include_directories(${Vulkan_INCLUDE_DIRS})
        else()
            include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        endif()
        # Placeholder when using -framework; static mode sets the .a below in engine CMake.
        set(EVENGINE_VULKAN_LIB "" CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan (iOS MoltenVK ${_eve_moltenvk_link_mode}): ${EVENGINE_MOLTENVK_XCFRAMEWORK}")
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
        message(FATAL_ERROR
            "Vulkan SDK is required but was not found via find_package(Vulkan).\n"
            "  Windows:\n"
            "    1. Install the LunarG Vulkan SDK from https://vulkan.lunarg.com/\n"
            "    2. Confirm system env VULKAN_SDK points at C:/VulkanSDK/<version>\n"
            "       and %VULKAN_SDK%/Bin is on PATH (for glslc)\n"
            "    3. Fully restart the terminal / Cursor, then re-run CMake\n"
            "       (IDE processes often miss newly set Machine env vars)\n"
            "  Linux:\n"
            "    Install libvulkan-dev (+ glslang-tools), or the LunarG SDK and set VULKAN_SDK\n"
            "  Current VULKAN_SDK='$ENV{VULKAN_SDK}'")
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
            EVENGINE_ANDROID EVENGINE_IOS)
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


# Ensure third-party sources exist at configure time.
# ExternalProject only downloads during the build of target `third-party`, but
# modules (e.g. eve_imgui) list files under third-party/ at configure time.
function(ensure_third_party_checkout)
    set(_eve_tp_dir "${CMAKE_SOURCE_DIR}/third-party")
    if(EXISTS "${_eve_tp_dir}/CMakeLists.txt")
        message(STATUS "third-party: using existing ${_eve_tp_dir}")
        return()
    endif()

    message(STATUS "third-party: cloning https://github.com/EVEngine/third-party ...")
    find_package(Git REQUIRED)
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch main --recurse-submodules
            https://github.com/EVEngine/third-party.git "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_clone_result
        OUTPUT_VARIABLE _eve_tp_clone_out
        ERROR_VARIABLE _eve_tp_clone_err
    )
    if(NOT _eve_tp_clone_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to clone third-party (exit ${_eve_tp_clone_result}).\n"
            "${_eve_tp_clone_out}${_eve_tp_clone_err}\n"
            "Manually run: git clone --recurse-submodules https://github.com/EVEngine/third-party ${_eve_tp_dir}")
    endif()
    # Shallow + recurse can leave nested submodules empty on some runners; force-init.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_sub_result
        OUTPUT_VARIABLE _eve_tp_sub_out
        ERROR_VARIABLE _eve_tp_sub_err
    )
    if(NOT _eve_tp_sub_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to init third-party submodules (exit ${_eve_tp_sub_result}).\n"
            "${_eve_tp_sub_out}${_eve_tp_sub_err}")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/imgui/imgui.cpp")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing imgui/imgui.cpp).\n"
            "Check ${_eve_tp_dir}")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/medialoader/CMakeLists.txt")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing medialoader submodule).\n"
            "Check ${_eve_tp_dir}/medialoader")
    endif()
    if(NOT EXISTS "${_eve_tp_dir}/medialoader/model-libs/assimp/CMakeLists.txt")
        message(FATAL_ERROR
            "third-party clone is incomplete (missing medialoader/assimp submodule).\n"
            "Check ${_eve_tp_dir}/medialoader/model-libs/assimp")
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

    # macOS/Linux/iOS: keep third-party as Release (stable). A Debug deps build
    # would inherit Poco's CMAKE_DEBUG_POSTFIX=d and rename physfs → physfsd,
    # while the engine still links -lphysfs.
    # Windows: Debug engine must get Debug deps (*mdd.lib), otherwise CI links
    # *mdd against Release *md installs (LNK1104).
    set(_eve_tp_build_type "Release")
    if(WIN32)
        set(_eve_tp_build_type "${CMAKE_BUILD_TYPE}")
    endif()
    set(_eve_tp_cmake_args
        -DCMAKE_BUILD_TYPE=${_eve_tp_build_type}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
    )
    # Windows only: force md/mdd before any add_subdirectory so squirrel/OpenAL
    # match the names the engine already links. Do not set these on Apple/Linux.
    if(WIN32)
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_DEBUG_POSTFIX=mdd
            -DCMAKE_RELEASE_POSTFIX=md
            -DCMAKE_MINSIZEREL_POSTFIX=md
            -DCMAKE_RELWITHDEBINFO_POSTFIX=md
        )
    endif()
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

    # --config is required for multi-config generators (VS); ignored by Ninja.
    set(_eve_tp_build_cmd ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}/ --target install --config ${_eve_tp_build_type} --parallel 8)

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

    ensure_third_party_checkout()
    # EVEngine's database module uses POCO Data and its bundled SQLite connector.
    # The third-party aggregate disables these by default to reduce its footprint;
    # enable the two targets in the checked-out aggregate before configuring it.
    set(_eve_tp_cmake "${CMAKE_CURRENT_SOURCE_DIR}/third-party/CMakeLists.txt")
    file(READ "${_eve_tp_cmake}" _eve_tp_contents)
    string(REPLACE "set(ENABLE_DATA OFF CACHE BOOL \"\" FORCE)"
                   "set(ENABLE_DATA ON CACHE BOOL \"\" FORCE)" _eve_tp_contents "${_eve_tp_contents}")
    string(REPLACE "set(ENABLE_DATA_SQLITE OFF CACHE BOOL \"\" FORCE)"
                   "set(ENABLE_DATA_SQLITE ON CACHE BOOL \"\" FORCE)" _eve_tp_contents "${_eve_tp_contents}")
    file(WRITE "${_eve_tp_cmake}" "${_eve_tp_contents}")
    check_third_party_project(third-party "https://github.com/EVEngine/third-party")

    ADD_CUSTOM_TARGET(deps DEPENDS third-party)
endfunction()

main()
