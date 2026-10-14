# Engine include roots and platform graphics SDK discovery.
include_guard(GLOBAL)

# Mark a third-party include root as "external" for strict builds, so the
# warning-as-error flags stay scoped to EVEngine's own code:
#   MSVC: /external:I + /external:W0 (does not change search order)
#   GCC/Clang: -isystem (suppresses header warnings from template
#              instantiations), except the third-party install tree: on GCC a
#              directory listed in both -I and -isystem has its -I entry
#              ignored and is searched after every -I, which would flip which
#              <vulkan/vulkan.hpp> wins on GCC (the vendored 261 must beat the SDK's).
function(eve_external_include dir)
    if(EVENGINE_ENABLE_STRICT_WARNINGS)
        if(MSVC)
            target_compile_options(eve_engine_includes INTERFACE "/external:I${dir}")
        else()
            # The third-party install tree stays a plain -I on GCC (see the
            # function comment); everything else (external/*, header-only
            # vendored roots) can be marked system without reordering.
            if(NOT dir MATCHES "third-party-(binary|releases)")
                target_compile_options(eve_engine_includes INTERFACE "-isystem${dir}")
            endif()
        endif()
    endif()
endfunction()


# This function will set the include directories for the project
function(set_include)
    target_include_directories(eve_engine_includes INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts
        ${CMAKE_BINARY_DIR}/src/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        # Override only zeroerr/unittest.h so fixture-generated types include
        # their fixture name. This avoids cross-TU ODR collisions under LTO
        # without mutating the pinned zeroerr submodule checkout.
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/overrides/zeroerr
        # sunxfancy/ECS.hpp + ik.hpp (git submodules) + zeroerr headers
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ik.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/external/zeroerr/include
        # DTL is header-only and includes itself through the DTL/... prefix.
        # Its prebuilt install is flat, so keep the pinned source include root
        # authoritative in both source and prebuilt dependency modes.
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include
        # sunxfancy/VKBuilder (git submodule) — header-only Vulkan wrapper. Exposed
        # globally (like the old third-party flattened install) so any module that
        # pulls in Graphics.h can resolve <vkbuilder.hpp>.
        ${EVENGINE_VKBUILDER_INCLUDE_DIR}
    )
    if(EVENGINE_VULKAN_USE_VMA AND NOT EVENGINE_WEBGPU AND NOT EMSCRIPTEN)
        # The Vulkan instance is deliberately created against the 1.0 contract.
        # Keep VMA on that same contract so Android does not statically import
        # newer core entry points that its loader only exposes dynamically.
        target_compile_definitions(eve_engine_includes INTERFACE
            VKB_ENABLE_VMA=1
            VMA_VULKAN_VERSION=1000000
            VMA_STATIC_VULKAN_FUNCTIONS=0
            VMA_DYNAMIC_VULKAN_FUNCTIONS=1
        )
    endif()
    if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR)
        # Raw source-checkout headers for medialoader/assimp. In prebuilt
        # mode (EVENGINE_THIRD_PARTY_BINARY_DIR) these headers come from the
        # install tree (include/medialoader and include/assimp);
        # mixing source headers with prebuilt libraries silently produces
        # ABI/behavior drift when the two are not the same commit.
        target_include_directories(eve_engine_includes INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/model-libs/assimp/include)
        # Hostless/core profiles still compile selected domain translation units
        # without building the aggregate third-party project. Expose the pinned
        # source headers for those compile-only targets; normal host builds still
        # receive the same roots in addition to the installed prefix.
        target_include_directories(eve_engine_includes INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/SDL2/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/squirrel/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/simplesquirrel/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/Foundation/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/JSON/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/Net/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/XML/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/Data/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/poco/Data/SQLite/include)
        # Third-party headers are external for warning purposes on strict
        # builds (vendored header-only libraries).
        eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/include)
        eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/model-libs/assimp/include)
        eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
    endif()
    # Third-party headers are external for warning purposes on MSVC strict
    # builds (submodule / vendored header-only libraries).
    eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp/src)
    eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/external/ik.hpp)
    eve_external_include(${CMAKE_CURRENT_SOURCE_DIR}/external/zeroerr/include)
    eve_external_include(${EVENGINE_VKBUILDER_INCLUDE_DIR})

    # Prefer system Vulkan SDK (find_package). No vendored vulkan-1.lib fallback.
    # On Apple desktop, MoltenVK from the LunarG Vulkan SDK is required.
    # On iOS, link/embed the SDK's MoltenVK.xcframework (iphoneos arm64 slice).
    # On Android, link the NDK loader (libvulkan.so).
    #
    # WebGPU builds (browser WASM or native Dawn) use webgpu.h and do not touch
    # the Vulkan SDK at all; the Vulkan backend directories are excluded by the
    # source scanner, so this probe would only ever FATAL_ERROR a valid build.
    if(NOT EVENGINE_BUILD_HOST)
        message(STATUS "Hostless profile: skipping Vulkan SDK probe")
        set(EVENGINE_VULKAN_LIB "" CACHE INTERNAL "Vulkan link target (unused by hostless profiles)")
        return()
    endif()
    if(EVENGINE_WEBGPU)
        message(STATUS "WebGPU build: skipping Vulkan SDK probe")
        set(EVENGINE_VULKAN_LIB "" CACHE INTERNAL "Vulkan link target (unused on webgpu)")
        return()
    endif()
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
    # LunarG Linux SDK 1.4.357+ may package the loader in its own install
    # prefix instead of directly under $VULKAN_SDK/lib. FindVulkan only probes
    # the legacy location, so retry with the packaged loader while retaining
    # the headers and shader tools discovered from the SDK root.
    if(NOT Vulkan_FOUND AND UNIX AND NOT APPLE
       AND DEFINED ENV{VULKAN_SDK}
       AND EXISTS "$ENV{VULKAN_SDK}/lib/VulkanLoader/lib/libvulkan.so")
        set(Vulkan_LIBRARY
            "$ENV{VULKAN_SDK}/lib/VulkanLoader/lib/libvulkan.so"
            CACHE FILEPATH "Vulkan loader library" FORCE)
        find_package(Vulkan QUIET)
    endif()
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        find_library(ANDROID_VULKAN_LIB vulkan)
        if(NOT ANDROID_VULKAN_LIB)
            message(FATAL_ERROR "Android NDK libvulkan not found (need ANDROID_PLATFORM >= 24)")
        endif()
        if(Vulkan_INCLUDE_DIRS)
            target_include_directories(eve_engine_includes INTERFACE ${Vulkan_INCLUDE_DIRS})
        else()
            target_include_directories(eve_engine_includes INTERFACE
                ${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        endif()
        set(EVENGINE_VULKAN_LIB ${ANDROID_VULKAN_LIB} CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan (Android): ${ANDROID_VULKAN_LIB}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # Resolve MoltenVK for iphoneos arm64.
        # LunarG ships two different xcframeworks:
        #   iOS/lib/.../ios-arm64/MoltenVK.framework  (dynamic; preferred, embeddable)
        #   macOS/lib/.../ios-arm64/libMoltenVK.a      (static; CI zip often only has this)
        # Prefer the framework; fall back to the static archive.
        # Pick the xcframework slice matching the active SDK: simulator builds
        # need the universal ios-arm64_x86_64-simulator framework, not the
        # device-only ios-arm64 one.
        if(CMAKE_OSX_SYSROOT MATCHES "simulator")
            set(_eve_mvk_slice "ios-arm64_x86_64-simulator")
        else()
            set(_eve_mvk_slice "ios-arm64")
        endif()
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
            set(_slice "${_abs}/${_eve_mvk_slice}")
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
            target_include_directories(eve_engine_includes INTERFACE ${Vulkan_INCLUDE_DIRS})
        else()
            target_include_directories(eve_engine_includes INTERFACE
                ${CMAKE_CURRENT_SOURCE_DIR}/third-party/vulkan)
        endif()
        # Placeholder when using -framework; static mode sets the .a below in engine CMake.
        set(EVENGINE_VULKAN_LIB "" CACHE INTERNAL "Vulkan link target")
        message(STATUS "Vulkan (iOS MoltenVK ${_eve_moltenvk_link_mode}): ${EVENGINE_MOLTENVK_XCFRAMEWORK}")
    elseif(Vulkan_FOUND AND Vulkan_INCLUDE_DIRS AND Vulkan_LIBRARIES)
        message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")
        if(APPLE AND EVENGINE_THIRD_PARTY_BINARY_DIR)
            # macOS source builds use the SDK's Vulkan-Hpp. Preserve that choice
            # with prebuilt dependencies so every TU sees the same dispatcher layout.
            target_include_directories(eve_engine_includes BEFORE INTERFACE ${Vulkan_INCLUDE_DIRS})
        else()
            target_include_directories(eve_engine_includes INTERFACE ${Vulkan_INCLUDE_DIRS})
        endif()
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
