cmake_minimum_required(VERSION 3.21)

set(EVENGINE_COMPILER_CACHE "AUTO" CACHE STRING
    "Compiler cache mode: AUTO detects sccache, SCCACHE requires it, OFF disables auto-detection")
set_property(CACHE EVENGINE_COMPILER_CACHE PROPERTY STRINGS AUTO SCCACHE OFF)
string(TOUPPER "${EVENGINE_COMPILER_CACHE}" _eve_compiler_cache_mode)
if(NOT _eve_compiler_cache_mode MATCHES "^(AUTO|SCCACHE|OFF)$")
    message(FATAL_ERROR
        "EVENGINE_COMPILER_CACHE must be AUTO, SCCACHE, or OFF "
        "(got: ${EVENGINE_COMPILER_CACHE})")
endif()

set(_eve_sccache_enabled OFF)
if(_eve_compiler_cache_mode STREQUAL "AUTO" AND
   (CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER))
    set(_eve_compiler_cache_status "custom launcher supplied")
elseif(NOT _eve_compiler_cache_mode STREQUAL "OFF")
    if(EVENGINE_SCCACHE_EXECUTABLE AND NOT EXISTS "${EVENGINE_SCCACHE_EXECUTABLE}")
        unset(EVENGINE_SCCACHE_EXECUTABLE CACHE)
    endif()
    find_program(EVENGINE_SCCACHE_EXECUTABLE NAMES sccache
        DOC "Path to the sccache compiler cache")
    if(EVENGINE_SCCACHE_EXECUTABLE)
        # Disable sccache's direct mode for both local and CI builds. The
        # conservative path hashes the complete preprocessed input before a
        # hit, including the contents of every header that was actually used.
        set(_eve_sccache_launcher
            "${CMAKE_COMMAND}" -E env SCCACHE_DIRECT=false
            "${EVENGINE_SCCACHE_EXECUTABLE}")
        set(CMAKE_C_COMPILER_LAUNCHER ${_eve_sccache_launcher})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${_eve_sccache_launcher})
        set(_eve_sccache_enabled ON)
        set(_eve_compiler_cache_status "sccache (${EVENGINE_SCCACHE_EXECUTABLE})")
        # CMP0141 must be NEW before project() for the first-class MSVC debug
        # information setting below to replace /Zi instead of appending to it.
        if(POLICY CMP0141)
            cmake_policy(SET CMP0141 NEW)
        endif()
    elseif(_eve_compiler_cache_mode STREQUAL "SCCACHE")
        message(FATAL_ERROR
            "EVENGINE_COMPILER_CACHE=SCCACHE requires sccache on PATH or "
            "-DEVENGINE_SCCACHE_EXECUTABLE=<path>")
    else()
        set(_eve_compiler_cache_status "disabled (sccache not found)")
    endif()
else()
    set(_eve_compiler_cache_status "disabled")
endif()

project(EVEngine)

if(_eve_sccache_enabled AND MSVC)
    if(POLICY CMP0141)
        # Embed debug information in each object. Cached objects are then
        # self-contained and parallel cl.exe processes do not share a PDB.
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
            "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    else()
        # CMake 3.21-3.24 have no MSVC debug-information abstraction. /Z7 is
        # appended after CMake's default /Zi, and cl.exe honors the last flag.
        add_compile_options(/Z7)
    endif()
endif()
message(STATUS "Compiler cache: ${_eve_compiler_cache_status}")

set(EVENGINE_MAJOR_VERSION "0")
set(EVENGINE_MINOR_VERSION "3")
set(EVENGINE_PATCH_VERSION "0")
set(EVENGINE_DEV_VERSION "-dev")

set(EVENGINE_VERSION "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}${EVENGINE_DEV_VERSION}")

option(BUILD_TESTING "enable testing" ON)
option(EVENGINE_BUILD_HOST
    "Build the interactive/native eve host (disabled by core/headless/server profiles)" ON)
option(EVENGINE_BUILD_PROFILE_CHECKS
    "Build independent public-header and optional-capability profile checks" OFF)
option(EVENGINE_BUILD_DEMO "Build optional Demo module (procedural demo assets + default demo.nut)" ON)
set(EVENGINE_THIRD_PARTY_BINARY_DIR "" CACHE PATH
    "Use a prebuilt third-party install tree (include/lib/bin) without rebuilding or modifying it")
set(EVENGINE_PREBUILT_THIRD_PARTY "" CACHE PATH
    "Compatibility alias for EVENGINE_THIRD_PARTY_BINARY_DIR")
set(EVENGINE_THIRD_PARTY_ROOT "" CACHE PATH
    "Deprecated compatibility alias for EVENGINE_THIRD_PARTY_BINARY_DIR")
if(EVENGINE_PREBUILT_THIRD_PARTY)
    message(DEPRECATION
        "EVENGINE_PREBUILT_THIRD_PARTY is deprecated; use EVENGINE_THIRD_PARTY_BINARY_DIR")
endif()
if(EVENGINE_THIRD_PARTY_ROOT)
    message(DEPRECATION
        "EVENGINE_THIRD_PARTY_ROOT is deprecated; use EVENGINE_THIRD_PARTY_BINARY_DIR")
endif()
if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR)
    if(EVENGINE_PREBUILT_THIRD_PARTY)
        set(EVENGINE_THIRD_PARTY_BINARY_DIR "${EVENGINE_PREBUILT_THIRD_PARTY}" CACHE PATH
            "Use a prebuilt third-party install tree" FORCE)
    elseif(EVENGINE_THIRD_PARTY_ROOT)
        set(EVENGINE_THIRD_PARTY_BINARY_DIR "${EVENGINE_THIRD_PARTY_ROOT}" CACHE PATH
            "Use a prebuilt third-party install tree" FORCE)
    endif()
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party_pin.cmake)
option(EVENGINE_ANDROID_TEST_APP
    "Build the unit_test suite as an Android test app (libmain.so entry replaced by the zeroerr runner)"
    OFF)
option(EVENGINE_IOS_TEST_APP
    "Build the unit_test suite as an iOS test app (eve executable replaced by the zeroerr runner)"
    OFF)
option(EVENGINE_ENABLE_ASSERTS
    "Keep zeroerr ASSERT checks (parameter validation, invariants) in non-Debug builds.
     Debug builds always enable them; this option is for Release-style builds."
    OFF)
option(EVENGINE_ENABLE_STRICT_WARNINGS
    "Treat compiler warnings as errors on EVEngine's own targets.
     GCC/Clang: -Wall -Wextra -Werror (unused-parameter and
     missing-field-initializers are suppressed until the legacy callback API
     and Vulkan aggregate-init debt are cleaned up).
     MSVC: /W4 /WX.
     Third-party targets are not affected."
    OFF)
message("Build UnitTest: " ${BUILD_TESTING})
message("Build native host: " ${EVENGINE_BUILD_HOST})
message("Build profile checks: " ${EVENGINE_BUILD_PROFILE_CHECKS})
message("Build Demo module: " ${EVENGINE_BUILD_DEMO})
message("Build Android test app: " ${EVENGINE_ANDROID_TEST_APP})
message("Build iOS test app: " ${EVENGINE_IOS_TEST_APP})
message("Enable asserts in non-Debug builds: " ${EVENGINE_ENABLE_ASSERTS})
message("Enable strict warnings (warnings as errors): " ${EVENGINE_ENABLE_STRICT_WARNINGS})
if (CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "The build type (Debug, Release)")
endif()

include(CTest)

set(_vkbuilder_header "${CMAKE_CURRENT_SOURCE_DIR}/external/VKBuilder/include/vkbuilder.hpp")
set(EVENGINE_VKBUILDER_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/vkbuilder")
if(EXISTS "${_vkbuilder_header}")
    file(READ "${_vkbuilder_header}" _vkbuilder_source)
    set(_vkbuilder_original [=[  PipelineBuilder& setColorBlending(vk::PipelineColorBlendStateCreateInfo color_blending) {
    use_default_color_blending = false;
    this->color_blending = color_blending;
    return *this;
  }]=])
    set(_vkbuilder_fixed [=[  PipelineBuilder& setColorBlending(vk::PipelineColorBlendStateCreateInfo color_blending) {
    use_default_color_blending = false;
    this->color_blending = color_blending;
    if (color_blending.attachmentCount > 0 && color_blending.pAttachments) {
      colorBlendAttachments.assign(color_blending.pAttachments,
                                   color_blending.pAttachments + color_blending.attachmentCount);
    } else {
      colorBlendAttachments.clear();
    }
    return *this;
  }]=])
    string(FIND "${_vkbuilder_source}" "${_vkbuilder_original}" _vkbuilder_patch_offset)
    if(_vkbuilder_patch_offset EQUAL -1)
        message(FATAL_ERROR "Pinned VKBuilder no longer matches the custom-blending compatibility fix")
    endif()
    string(REPLACE "${_vkbuilder_original}" "${_vkbuilder_fixed}" _vkbuilder_source
        "${_vkbuilder_source}")
    file(MAKE_DIRECTORY "${EVENGINE_VKBUILDER_INCLUDE_DIR}")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/external/VKBuilder/include/"
        DESTINATION "${EVENGINE_VKBUILDER_INCLUDE_DIR}")
    file(WRITE "${EVENGINE_VKBUILDER_INCLUDE_DIR}/vkbuilder.hpp" "${_vkbuilder_source}")
endif()


# This is the main function of the build script
function(main)
    # Central include/link-usage target for every engine module, platform
    # library and test runner. Replaces the old global include_directories() /
    # link_directories() sprawl: targets opt in by linking eve_engine_includes,
    # and the third-party install trees register their include/link dirs here.
    add_library(eve_engine_includes INTERFACE)
    get_available_build()
    # Resolve the manifest before loading the aggregate dependency project. The
    # selected third-party groups are passed to that project, so a trimmed
    # profile does not configure/build an unrelated codec, renderer or database.
    include(${CMAKE_SOURCE_DIR}/cmake/module_manifest.cmake)
    include(${CMAKE_SOURCE_DIR}/cmake/thirdparty_libs.cmake)
    include(${CMAKE_SOURCE_DIR}/cmake/system_libraries.cmake)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT EVENGINE_PROFILE)
        # The browser runtime drops the modules whose third-party deps were cut.
        set(EVENGINE_PROFILE "web")
    endif()
    # DevTools (DAP / MCP / snapshot) rely on Poco JSON+Net and have never been
    # part of the mobile or browser runtimes.
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android") OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
       OR EVENGINE_IOS OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(EVENGINE_MODULE_devtools OFF)
    endif()
    eve_resolve_modules()

    load_third_party()
    load_webgpu_backend()
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
    # /EHsc: CMAKE_CXX_FLAGS is empty in this tree, so MSVC/clang-cl otherwise compile
    # with exceptions off. clangd then errors on try/throw (zeroerr ASSERT, Exception).
    if(MSVC)
        # /FS serializes writes to a target PDB when MSBuild schedules several cl.exe jobs.
        # /bigobj: /GL (LTO, check_ipo()) puts every function into a COMDAT section,
        # and large TUs (e.g. Graphics.cpp) exceed the default COFF section limit (C1128).
        add_compile_options(/utf-8 /EHsc /FS /bigobj)
    endif()

    # Strict-warnings gate. Applied through the shared INTERFACE target so every
    # engine module, platform library and test runner inherits it while
    # third-party (ExternalProject / add_subdirectory deps) stays untouched.
    # Enabled by CI via -DEVENGINE_ENABLE_STRICT_WARNINGS=ON; local opt-in:
    #   make build/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_STRICT_WARNINGS=ON
    if(EVENGINE_ENABLE_STRICT_WARNINGS)
        target_compile_options(eve_engine_includes INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            # C4251/C4275: STL members/base classes in __declspec(dllexport)
            # classes are safe with MSVC's own STL; the warnings are noise for
            # an engine DLL. C4100 mirrors the GCC side's -Wno-unused-parameter:
            # callback APIs keep unused parameters for stability until the
            # legacy interface is cleaned up. C4244 loss-of-data conversions
            # stay enabled and are fixed at the call sites.
            # /external:W0 silences warnings from third-party headers marked
            # with eve_external_include() (defined below), so /W4 /WX applies
            # only to EVEngine's own code.
            # _CRT_SECURE_NO_WARNINGS: the engine deliberately uses standard C
            # APIs (getenv, etc.) for portability; MSVC's secure-CRT
            # deprecations are reviewed in code review instead of forcing
            # _s variants at every call site.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4251>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4275>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4100>
            # C4702: unreachable code emitted by third-party header-only code
            # (vkbuilder.hpp) at flow-analysis level; engine-side dead code is
            # removed at the call sites instead.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4702>
            # C4456/4457/4458: "declaration hides class member" family. The
            # legacy codebase pervasively names parameters after the member they
            # set (setWidth(int width) etc.); renaming ~60+ call sites is a
            # dedicated cleanup PR, not a gate blocker. C4244/C4267/C4099/
            # C4389/C4189 stay enabled and are fixed at the call sites.
            $<$<CXX_COMPILER_ID:MSVC>:/wd4456>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4457>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4458>
            $<$<CXX_COMPILER_ID:MSVC>:/external:W0>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
            # -Wno-unused-function: shared per-TU helper headers (GraphicsInternal.h,
            # SceneInternal.h, ...) deliberately give every including TU its own
            # internal copy, so a helper unused in one TU is structural, not a
            # defect. Unused variables and unused-but-set stay enabled.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-function>
            # Third-party template headers (simplesquirrel) emit these during
            # instantiation in engine TUs. The third-party install root stays a
            # plain -I (marking it -isystem would demote it below the Vulkan SDK
            # -I and flip which <vulkan/vulkan.hpp> wins, breaking VKBuilder),
            # so the leaks cannot be silenced per-directory on GCC and are
            # downgraded from errors to warnings here. Engine-side instances of
            # these warnings are still reported.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=unused-variable>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=unused-but-set-parameter>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=sign-compare>
            # Poco::Data Date/Time implicit-copy deprecations and
            # -Wdelete-non-virtual-dtor (the ECS.hpp base Entity has virtuals
            # but no virtual dtor) fire from third-party headers during template
            # instantiation in engine TUs; same treatment as above.
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=deprecated-copy>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=delete-non-virtual-dtor>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>)
        target_compile_definitions(eve_engine_includes INTERFACE
            $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
            # POSIX-name deprecations (fileno etc.) are the same category as
            # the secure-CRT warnings above; the ISO C++ name is used where
            # practical.
            $<$<CXX_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>)
        # Vendored third-party source roots (third-party/Box2D, imgui, ...) are
        # external for warning purposes. Safe on GCC: the third-party install
        # tree stays a plain -I and is searched first, so the vendored
        # vulkan.hpp still beats the SDK's.
        eve_external_include(${CMAKE_SOURCE_DIR}/third-party)
    endif()

    # zeroerr before modules so Debug ASSERT headers/defs are available when compiling engine code.
    set(BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(USE_MOLD OFF CACHE BOOL "" FORCE)
    set(COLORFUL_OUTPUT OFF CACHE STRING "" FORCE)
    if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        if(EVENGINE_ANDROID_TEST_APP)
            message(STATUS "Android build: unit tests enabled (Android test app)")
        else()
            set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
            message(STATUS "Android build: unit tests disabled")
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(EVENGINE_IOS_TEST_APP)
            message(STATUS "iOS build: unit tests enabled (iOS test app)")
        else()
            set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
            message(STATUS "iOS build: unit tests disabled")
        endif()
    endif()
    # WebGPU (browser WASM + native Dawn): unit tests are disabled by default —
    # most tests link Vulkan-only helpers. Native Dawn builds can opt back in
    # with -DBUILD_TESTING=ON once the test tree is backend-neutral.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
        message(STATUS "Emscripten/webgpu build: unit tests disabled")
        # Emdawnwebgpu port: provides <webgpu/webgpu.h> + <webgpu/webgpu_cpp.h>
        # and the browser WebGPU JS glue. Directory-scope so every module target
        # (EVGraphics, EVUI, EVGpgpu, eve_imgui, ...) gets it at compile + link.
        add_compile_options(--use-port=emdawnwebgpu)
        add_link_options(--use-port=emdawnwebgpu)
        # SDL2, zlib and FreeType come from Emscripten ports (vendored trees are
        # skipped
        # by third-party/CMakeLists.txt on Emscripten). -sUSE_SDL=2 must also be
        # a compile flag so TUs that include <SDL.h> (imgui_impl_sdl, window,
        # event, mouse, keyboard, ...) resolve the port headers. FreeType must
        # likewise be enabled while compiling so <ft2build.h> is installed in
        # and exposed from the Emscripten sysroot.
        add_compile_options(-sUSE_SDL=2 -sUSE_ZLIB -sUSE_FREETYPE=1)
        add_link_options(-sUSE_SDL=2 -sUSE_ZLIB -sUSE_FREETYPE=1)
        # PROXY_TO_PTHREAD turns on shared memory; every TU must be compiled
        # with atomics/bulk-memory or wasm-ld rejects the final link.
        add_compile_options(-matomics -mbulk-memory)
        # The trimmed module set for the WASM runtime is the "web" profile,
        # selected below from cmake/module_manifest.cmake.
        # Emscripten 6.x vendors a fork of CheckTypeSize.cmake that is
        # incompatible with CMake >= 4.2 (mpg123/physfs/... try_compile fail
        # with "/CheckTypeSize.c.in does not exist"). Force a CMake 3.29.x for
        # Emscripten builds: it must be on PATH (or pip-installed) so that both
        # this configure and the ExternalProject sub-builds use it.
        find_program(EVENGINE_WEB_CMAKE NAMES cmake
            PATHS "$ENV{APPDATA}/Python/Python314/Scripts"
            "$ENV{APPDATA}/Python/Python311/Scripts"
            "$ENV{USERPROFILE}/.local/bin")
        if(EVENGINE_WEB_CMAKE)
            execute_process(COMMAND "${EVENGINE_WEB_CMAKE}" --version
                OUTPUT_VARIABLE _eve_cmake_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(_eve_cmake_ver MATCHES "cmake version 3\.2[0-9]\.")
                message(STATUS "Emscripten build: using CMake ${_eve_cmake_ver} from ${EVENGINE_WEB_CMAKE}")
                set(CMAKE_COMMAND "${EVENGINE_WEB_CMAKE}" CACHE INTERNAL "CMake executable (forced 3.29 for Emscripten)")
            else()
                message(WARNING "Emscripten build needs CMake 3.29.x (found: ${_eve_cmake_ver}). "
                    "Install with: python -m pip install cmake==3.29.6")
            endif()
        else()
            message(WARNING "Emscripten build needs CMake 3.29.x on PATH (pip: python -m pip install cmake==3.29.6)")
        endif()
    elseif(BUILD_PLATFORM STREQUAL "webgpu" AND BUILD_TESTING)
        message(STATUS "webgpu (native Dawn) build: unit tests enabled")
    endif()
    add_subdirectory(external/zeroerr)
    target_compile_definitions(zeroerr PUBLIC ZEROERR_NO_MAIN)
    # Strip ASSERT macros in non-Debug builds (upstream ZEROERR_NO_ASSERT under
    # NDEBUG is commented out), unless assertions were explicitly enabled with
    # -DEVENGINE_ENABLE_ASSERTS=ON. See src/engine/common/Assert.h.
    if(NOT EVENGINE_ENABLE_ASSERTS)
        add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:ZEROERR_NO_ASSERT>)
    endif()

    if(EVENGINE_BUILD_HOST)
        # Platform CMake may set EVENGINE_PLATFORM_LIB; clear stale cache from other platforms.
        unset(EVENGINE_PLATFORM_LIB CACHE)
        add_subdirectory(platform/${BUILD_PLATFORM})
        # Platform libraries (android / ios / macosx / webgpu) also compile against
        # the shared engine include set.
        if(EVENGINE_PLATFORM_LIB AND TARGET ${EVENGINE_PLATFORM_LIB})
            target_link_libraries(${EVENGINE_PLATFORM_LIB} PUBLIC eve_engine_includes)
        endif()
    else()
        set(BUILD_TESTING OFF CACHE BOOL "enable testing" FORCE)
        message(STATUS "Hostless profile: platform runtime and unit_test disabled")
    endif()
    # Build metadata: bake the engine git commit/dirty + build time + the
    # third-party version into a generated header consumed by
    # src/engine/common/Version.cpp, so `eve --version` (and any binary) can
    # report the exact source state it was built from.
    set(EVENGINE_GIT_COMMIT "unknown")
    set(EVENGINE_GIT_DIRTY 0)
    find_package(Git QUIET)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _eve_git_commit_result
            OUTPUT_VARIABLE _eve_git_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_eve_git_commit_result EQUAL 0 AND _eve_git_commit)
            set(EVENGINE_GIT_COMMIT "${_eve_git_commit}")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE _eve_git_dirty_result
            OUTPUT_VARIABLE _eve_git_dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_eve_git_dirty_result EQUAL 0 AND _eve_git_dirty)
            set(EVENGINE_GIT_DIRTY 1)
        endif()
    endif()
    string(TIMESTAMP EVENGINE_BUILD_TIME "%Y-%m-%d %H:%M:%S %z")
    if(NOT EVENGINE_TP_DESC)
        set(EVENGINE_TP_DESC "unknown")
    endif()
    add_subdirectory(src/engine)
    add_subdirectory(src/modules)
    if(EVENGINE_BUILD_HOST)
        add_subdirectory(src/scripts)
    endif()

    if(EVENGINE_BUILD_PROFILE_CHECKS)
        include(${CMAKE_SOURCE_DIR}/cmake/profile_checks.cmake)
    endif()

    if(EVENGINE_BUILD_HOST AND BUILD_TESTING)
        add_subdirectory(test)
    endif()

    # Doxygen documentation lives in docs/CMakeLists.txt (`--target docs`).
    add_subdirectory(docs)

    if(EVENGINE_BUILD_HOST)
        include(${CMAKE_SOURCE_DIR}/cmake/timidity_share.cmake)
        eve_stage_timidity_share()
        include(${CMAKE_SOURCE_DIR}/cmake/install_sdk.cmake)
    endif()
endfunction()


# This marco will create a module with the given name and folder
# It will scan the source code and create a xxx_src.txt file in the build folder
# When the build is triggered, it will check the real source code and see
# if there is any change. If there is, it will rescan the source code 

macro(create_module libname folder)
    # Join the exclude lists with ',' instead of ';': the rescan target below is
    # run through a shell (Ninja on Linux/macOS), where both ';' and '|' are
    # shell metacharacters (command separator / pipe) that would split or
    # execute the -D argument. ',' is safe unquoted on every shell (POSIX sh,
    # cmd.exe, emcc). The scan/rescan scripts split on ','.
    string(JOIN "," _eve_excl_dirs_arg ${EVENGINE_EXCLUDED_BACKEND_SUBDIRS})
    string(JOIN "," _eve_excl_files_arg ${EVENGINE_EXCLUDED_MODULE_FILES})
    execute_process(COMMAND ${CMAKE_COMMAND}
        -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}
        -Dmodule_name=${folder}
        -Dexclude_dirs=${_eve_excl_dirs_arg}
        -Dexclude_files=${_eve_excl_files_arg}
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
    # Opt the module into the shared engine include set (was global
    # include_directories before; see eve_engine_includes in main()).
    target_link_libraries(${libname} PUBLIC eve_engine_includes)

    add_custom_target(check_sources_${folder}
        ALL
        COMMAND ${CMAKE_COMMAND} 
        -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}
        -Dbuild_dir=${CMAKE_BINARY_DIR}
        -Dmodule_name=${folder}
        -Dexclude_dirs=${_eve_excl_dirs_arg}
        -Dexclude_files=${_eve_excl_files_arg}
        -P ${CMAKE_SOURCE_DIR}/cmake/rescan_source.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${folder}_src.txt)
    
    if(EVENGINE_PROFILE_HOSTLESS)
        # Hostless/core profile targets are compile-only boundaries. Their
        # headers come from the pinned checkout and they must not pull the
        # aggregate ExternalProject merely to prove source isolation.
        add_dependencies(${libname} check_sources_${folder})
    else()
        add_dependencies(${libname} third-party check_sources_${folder})
    endif()
endmacro()


# Mark a third-party include root as "external" for strict builds, so the
# warning-as-error flags stay scoped to EVEngine's own code:
#   MSVC: /external:I + /external:W0 (does not change search order)
#   GCC/Clang: -isystem (suppresses header warnings from template
#              instantiations), except the third-party install tree: on GCC a
#              directory listed in both -I and -isystem has its -I entry
#              ignored and is searched after every -I, which would flip which
#              <vulkan/vulkan.hpp> wins (the vendored 261 must beat the SDK's).
function(eve_external_include dir)
    if(EVENGINE_ENABLE_STRICT_WARNINGS)
        if(MSVC)
            target_compile_options(eve_engine_includes INTERFACE "/external:I${dir}")
        else()
            # The third-party install tree stays a plain -I on GCC (see the
            # function comment); everything else (external/*, header-only
            # vendored roots) can be marked system without reordering.
            if(NOT dir MATCHES "third-party-binary")
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
        # sunxfancy/VKBuilder (git submodule) — header-only Vulkan wrapper. Exposed
        # globally (like the old third-party flattened install) so any module that
        # pulls in Graphics.h can resolve <vkbuilder.hpp>.
        ${EVENGINE_VKBUILDER_INCLUDE_DIR}
    )
    if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR)
        # Raw source-checkout headers for medialoader/assimp/DTL. In prebuilt
        # mode (EVENGINE_THIRD_PARTY_BINARY_DIR) these headers come from the
        # install tree (include/medialoader, include/assimp, flat DTL headers);
        # mixing source headers with prebuilt libraries silently produces
        # ABI/behavior drift when the two are not the same commit.
        target_include_directories(eve_engine_includes INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/medialoader/model-libs/assimp/include
            # Dungeon Template Library (header-only)
            ${CMAKE_CURRENT_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
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
        target_include_directories(eve_engine_includes INTERFACE ${Vulkan_INCLUDE_DIRS})
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

    # Cross-compiling for Emscripten (web/WASM, browser WebGPU) must win over host detection.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        list(APPEND AVAILABLE_PLATFORM "webgpu")
        set(BUILD_PLATFORM "webgpu" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
        SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
    # Cross-compiling for Android (NDK toolchain) must win over host Apple/Linux detection.
    elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
        list(APPEND AVAILABLE_PLATFORM "android")
        set(BUILD_PLATFORM "android" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
        SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND AVAILABLE_PLATFORM "ios")
        set(BUILD_PLATFORM "ios" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
        SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
    elseif(UNIX)
        if(APPLE)
            list(APPEND AVAILABLE_PLATFORM "macosx")
            list(APPEND AVAILABLE_PLATFORM "ios")
            list(APPEND AVAILABLE_PLATFORM "android")
            list(APPEND AVAILABLE_PLATFORM "webgpu")
            set(BUILD_PLATFORM "macosx" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        else() # Linux
            list(APPEND AVAILABLE_PLATFORM "linux")
            list(APPEND AVAILABLE_PLATFORM "android")
            list(APPEND AVAILABLE_PLATFORM "webgpu")
            set(BUILD_PLATFORM "linux" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        endif()
    else()
        if(WIN32)
            list(APPEND AVAILABLE_PLATFORM "win32")
            list(APPEND AVAILABLE_PLATFORM "android")
            list(APPEND AVAILABLE_PLATFORM "webgpu")
            set(BUILD_PLATFORM "win32" CACHE STRING "The build platform (${AVAILABLE_PLATFORM})")
            MESSAGE("Build Platform:  ${BUILD_PLATFORM}")
            SET_PROPERTY(CACHE BUILD_PLATFORM PROPERTY STRINGS ${AVAILABLE_PLATFORM})
        endif()
    endif()

    # Clear previous platform defines so configure_file / #cmakedefine stay accurate
    # when switching BUILD_PLATFORM in the same build tree.
    foreach(_eve_plat_var IN ITEMS
            EVENGINE_WINDOWS EVENGINE_MACOSX EVENGINE_LINUX
            EVENGINE_ANDROID EVENGINE_IOS EVENGINE_WEBGPU)
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
    set_platform_var(webgpu EVENGINE_WEBGPU)

    # BUILD_PLATFORM selects the renderer/runtime profile, while native Dawn
    # still needs the host OS branches for filesystem, process and UTF-8 APIs.
    # Emscripten does not report WIN32, so browser builds remain WebGPU-only.
    if(BUILD_PLATFORM STREQUAL "webgpu" AND WIN32)
        set(EVENGINE_WINDOWS 1 CACHE INTERNAL "EVEngine native host platform define")
    endif()

    # Backend directories compiled out of the active build. Native backends
    # (graphics/vulkan, gpgpu/vulkan) are excluded from a webgpu build; the
    # webgpu backend is excluded from every other platform. ui/imgui stays in
    # the scan — ImGuiBackend.cpp is the EVUI implementation and gains a
    # webgpu branch instead.
    if(BUILD_PLATFORM STREQUAL "webgpu")
        set(EVENGINE_EXCLUDED_BACKEND_SUBDIRS "vulkan" CACHE INTERNAL "Backend subdirs excluded from webgpu builds")
    else()
        set(EVENGINE_EXCLUDED_BACKEND_SUBDIRS "webgpu" CACHE INTERNAL "Backend subdirs excluded from native builds")
    endif()

    # Poco-heavy sources trimmed from the WASM (Emscripten) build. The data
    # module keeps ByteData (needed by gpgpu/webgpu) but drops the JSON/XML/
    # DataModule files that require Poco. Font/FreeType is supported on WASM.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(EVENGINE_EXCLUDED_MODULE_FILES
            "DataModule.cpp;JsonDocument.cpp;XmlDocument.cpp;ImGuiHostPanels.cpp"
            CACHE INTERNAL "Exact basenames excluded from module source scan on Emscripten")
    else()
        set(EVENGINE_EXCLUDED_MODULE_FILES
            "ImGuiHostPanels.cpp"
            CACHE INTERNAL "Exact basenames excluded from module source scan")
    endif()

    message("Available: ${AVAILABLE_PLATFORM}")

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


# Ensure third-party sources exist at configure time and sit on the pinned
# commit. ExternalProject only downloads during the build of target
# `third-party`, but modules (e.g. eve_imgui) list files under third-party/
# at configure time.
function(ensure_third_party_checkout)
    set(_eve_tp_dir "${CMAKE_SOURCE_DIR}/third-party")
    find_package(Git REQUIRED)
    if(EXISTS "${_eve_tp_dir}/CMakeLists.txt")
        # An existing checkout is authoritative for the build, so verify it is
        # actually the pinned commit instead of silently compiling whatever
        # main happened to be at clone time (the ExternalProject GIT_TAG has
        # no effect when the source dir already exists).
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
            WORKING_DIRECTORY "${_eve_tp_dir}"
            RESULT_VARIABLE _eve_tp_head_result
            OUTPUT_VARIABLE _eve_tp_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT _eve_tp_head_result EQUAL 0)
            message(FATAL_ERROR
                "third-party exists at ${_eve_tp_dir} but is not a readable git checkout")
        endif()
        if(NOT _eve_tp_head STREQUAL EVENGINE_THIRD_PARTY_PIN)
            message(FATAL_ERROR
                "third-party checkout is at ${_eve_tp_head}, expected pinned ${EVENGINE_THIRD_PARTY_PIN}.\n"
                "Fix with:\n"
                "  git -C third-party fetch --depth 1 origin ${EVENGINE_THIRD_PARTY_PIN}\n"
                "  git -C third-party checkout --detach ${EVENGINE_THIRD_PARTY_PIN}\n"
                "then force a deps rebuild (remove build/third-party/<plat>/third-party-prefix),\n"
                "or override the pin with -DEVENGINE_THIRD_PARTY_PIN=<commit>.")
        endif()
        message(STATUS "third-party: pinned checkout ${_eve_tp_head} verified")
        return()
    endif()

    message(STATUS "third-party: cloning https://github.com/EVEngine/third-party at ${EVENGINE_THIRD_PARTY_PIN} ...")
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
    # The shallow main clone is not guaranteed to be the pinned commit; move
    # it onto the pin before submodules are synced.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} fetch --depth 1 origin ${EVENGINE_THIRD_PARTY_PIN}
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_fetch_result
        OUTPUT_VARIABLE _eve_tp_fetch_out
        ERROR_VARIABLE _eve_tp_fetch_err
    )
    if(NOT _eve_tp_fetch_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to fetch pinned third-party commit ${EVENGINE_THIRD_PARTY_PIN}.\n"
            "${_eve_tp_fetch_out}${_eve_tp_fetch_err}")
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} checkout --detach ${EVENGINE_THIRD_PARTY_PIN}
        WORKING_DIRECTORY "${_eve_tp_dir}"
        RESULT_VARIABLE _eve_tp_checkout_result
        OUTPUT_VARIABLE _eve_tp_checkout_out
        ERROR_VARIABLE _eve_tp_checkout_err
    )
    if(NOT _eve_tp_checkout_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to check out pinned third-party commit ${EVENGINE_THIRD_PARTY_PIN}.\n"
            "${_eve_tp_checkout_out}${_eve_tp_checkout_err}")
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

    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        get_filename_component(_eve_tp_binary "${EVENGINE_THIRD_PARTY_BINARY_DIR}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${_eve_tp_binary}/include" OR
           NOT IS_DIRECTORY "${_eve_tp_binary}/lib")
            message(FATAL_ERROR
                "EVENGINE_THIRD_PARTY_BINARY_DIR must contain include/ and lib/: "
                "${_eve_tp_binary}")
        endif()
        add_custom_target(${name})
        target_include_directories(eve_engine_includes INTERFACE "${_eve_tp_binary}/include")
        eve_external_include("${_eve_tp_binary}/include")
        # SDL2 and FreeType install their public headers one level below include/.
        # Their sources use <SDL.h> and <ft2build.h>, so expose those roots too.
        if(IS_DIRECTORY "${_eve_tp_binary}/include/SDL2")
            target_include_directories(eve_engine_includes INTERFACE
                "${_eve_tp_binary}/include/SDL2")
            eve_external_include("${_eve_tp_binary}/include/SDL2")
        endif()
        if(IS_DIRECTORY "${_eve_tp_binary}/include/freetype2")
            target_include_directories(eve_engine_includes INTERFACE
                "${_eve_tp_binary}/include/freetype2")
            eve_external_include("${_eve_tp_binary}/include/freetype2")
        endif()
        target_link_directories(eve_engine_includes INTERFACE "${_eve_tp_binary}/lib")
        message(STATUS "third-party: using read-only prebuilt tree ${_eve_tp_binary}")
        return()
    endif()

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
    # Pass the resolved logical groups into the isolated dependency project.
    # Comma encoding keeps one CMake argument intact across ExternalProject's
    # list expansion on POSIX, Ninja, MSBuild and Emscripten.
    string(JOIN "," _eve_tp_groups_arg ${EVE_THIRDPARTY_GROUPS})
    list(APPEND _eve_tp_cmake_args
        -DEVENGINE_THIRD_PARTY_GROUPS=${_eve_tp_groups_arg}
        -DEVENGINE_BUILD_HOST=${EVENGINE_BUILD_HOST})
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
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        # Third-party aggregates for the browser build use emcmake so they pick
        # up the emcc toolchain; Ninja keeps the many try_compile probes fast.
        find_program(_eve_ninja ninja REQUIRED)
        list(APPEND _eve_tp_cmake_args
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_SYSTEM_NAME=Emscripten
            -DCMAKE_MAKE_PROGRAM=${_eve_ninja}
        )
        set(_eve_tp_use_ninja TRUE)
        message(STATUS "third-party Emscripten toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    endif()

    # --config is required for multi-config generators (VS); ignored by Ninja.
    set(_eve_tp_build_cmd ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}/ --target install --config ${_eve_tp_build_type} --parallel 8)

    # Tracked patch applied at build time (never by mutating the checkout at
    # configure time). Paths in the patch are relative to the medialoader
    # submodule root. See cmake/patch_third_party.cmake.
    set(_eve_tp_patch_cmd ${CMAKE_COMMAND}
        -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/medialoader-smooth-normals.patch
        -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}/medialoader
        -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Second patch: export squirrel/ssq symbols from the win32 host binary
    # (paths relative to the third-party aggregate root).
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/third-party-squirrel-ssq-export.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Third patch: ssq class allocators must translate C++ exceptions from
    # module constructors into Squirrel errors. Without this, a throwing ctor
    # (e.g. Audio with no audio device) escapes the native call, corrupts the
    # VM's exception state and hangs during sq_close (SQInstance::Release).
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/simplesquirrel-constructor-exceptions.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Fourth patch: SimpleSquirrel's Object move constructor swaps an
    # uninitialized `weak` flag. Optimized arm64 builds can observe that UB
    # when script bindings pass Array/Object wrappers by value.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/simplesquirrel-object-move-init.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Fifth patch: mpg123's K&R-style signal handler type is rejected by GCC
    # 15; use the POSIX void(int) signature accepted by sigaction.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/mpg123-signal-handler.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}/medialoader
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Sixth patch: SDL2's Android sensor loop uses ALooper_pollAll, which the
    # NDK 27+ headers mark unavailable (obsoleted in Android 1). Use the
    # equivalent ALooper_pollOnce (same non-blocking semantics at timeout 0).
    # This is upstream SDL's own fix for libsdl-org/SDL#9792 (the exact NDK-27
    # build failure): commit 558630d "Use ALooper_pollOnce() instead of
    # ALooper_pollAll()" notes the existing draining loop already handles all
    # events, so no events are lost. SDL2 2.0.x never adopted it, hence the patch.
    # The sensor looper is dedicated (not the Android UI message loop), so this
    # cannot affect the main UI looper.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/sdl2-android-alooper-pollonce.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)
    # Seventh patch: Binding Contract constraints describe the argument as a
    # whole. Consume them at the expression's first factor so a string result
    # does not reject numeric literals nested in concatenations or ternaries.
    set(_eve_tp_patch_cmd ${_eve_tp_patch_cmd}
        COMMAND ${CMAKE_COMMAND}
            -DPATCH=${CMAKE_SOURCE_DIR}/cmake/patches/squirrel-binding-contract-expression-scope.patch
            -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
            -P ${CMAKE_SOURCE_DIR}/cmake/patch_third_party.cmake)

    # Stamp the git versions into the install tree after every install so
    # prebuilt-mode consumers (and eve's build info) can report exactly which
    # third-party commit the libraries were built from.
    set(_eve_tp_version_cmd ${CMAKE_COMMAND}
        -DPREFIX=${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}
        -DTP_DIR=${CMAKE_CURRENT_SOURCE_DIR}/${name}
        -DPLATFORM=${BUILD_PLATFORM}
        -DBUILD_TYPE=${_eve_tp_build_type}
        -P ${CMAKE_SOURCE_DIR}/cmake/write_third_party_version.cmake)

    if(_eve_tp_use_ninja)
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG ${EVENGINE_THIRD_PARTY_PIN}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            CMAKE_GENERATOR "Ninja"
            CMAKE_ARGS ${_eve_tp_cmake_args}
            PATCH_COMMAND ${_eve_tp_patch_cmd}
            BUILD_COMMAND ${_eve_tp_build_cmd} COMMAND ${_eve_tp_version_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    else()
        ExternalProject_Add(${name}
            GIT_REPOSITORY ${GIT_HTTPS_LINK}
            GIT_TAG ${EVENGINE_THIRD_PARTY_PIN}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${name}
            BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}/${TP_BUILD_PATH}
            CMAKE_ARGS ${_eve_tp_cmake_args}
            PATCH_COMMAND ${_eve_tp_patch_cmd}
            BUILD_COMMAND ${_eve_tp_build_cmd} COMMAND ${_eve_tp_version_cmd}
            BUILD_ALWAYS 0
            EXCLUDE_FROM_ALL 1
        )
    endif()
    target_include_directories(eve_engine_includes INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    eve_external_include(
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/include)
    target_link_directories(eve_engine_includes INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/build/${name}-binary/${TP_BUILD_PATH}/lib)
endfunction()

# Load third party project from github
function(load_third_party)
    # Debug gets its own third-party install tree (<plat>-debug); the other
    # supported single-config build types (Release, RelWithDebInfo, MinSizeRel)
    # share the release-style tree and the md library postfixes on Windows.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(DEBUG_APPENDIX "-debug")
    else()
        set(DEBUG_APPENDIX "")
    endif()
    message("Build Type: ${CMAKE_BUILD_TYPE} (third-party tree suffix '${DEBUG_APPENDIX}')")

    # Simulator builds cannot reuse the device third-party tree (the linker
    # rejects iphoneos Mach-O slices for an iphonesimulator target), so give
    # them their own install prefix instead of clobbering build/third-party-binary/ios-debug.
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND CMAKE_OSX_SYSROOT MATCHES "simulator")
        set(_eve_tp_platform "ios-simulator")
    else()
        set(_eve_tp_platform ${BUILD_PLATFORM})
    endif()
    set(TP_BUILD_PATH ${_eve_tp_platform}${DEBUG_APPENDIX})
    message("Third Party Build Path: ${TP_BUILD_PATH}")

    # The engine still compiles a small set of vendored sources (notably ImGui)
    # even when the compiled libraries come from a prebuilt install tree. Keep
    # the pinned checkout available on fresh CI runners as well as local builds.
    ensure_third_party_checkout()

    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        cmake_path(ABSOLUTE_PATH EVENGINE_THIRD_PARTY_BINARY_DIR NORMALIZE
                   OUTPUT_VARIABLE _eve_prebuilt_tp)
        if(NOT IS_DIRECTORY "${_eve_prebuilt_tp}/include" OR
           NOT IS_DIRECTORY "${_eve_prebuilt_tp}/lib")
            message(FATAL_ERROR
                "EVENGINE_THIRD_PARTY_BINARY_DIR must contain include/ and lib/: ${_eve_prebuilt_tp}")
        endif()
        message(STATUS "third-party: using read-only prebuilt install ${_eve_prebuilt_tp}")
        target_include_directories(eve_engine_includes INTERFACE "${_eve_prebuilt_tp}/include")
        eve_external_include("${_eve_prebuilt_tp}/include")
        target_link_directories(eve_engine_includes INTERFACE "${_eve_prebuilt_tp}/lib")
        set(EVENGINE_THIRD_PARTY_BINARY_ONLY ON CACHE INTERNAL
            "Use installed third-party prefix" FORCE)
        # Read the install tree's version stamp (written by
        # cmake/write_third_party_version.cmake at install time). A missing
        # stamp is tolerated with a warning (older prebuilt trees); a stamp
        # that disagrees with a present source checkout is a hard error,
        # because the engine would otherwise compile against new source
        # headers while linking stale prebuilt libraries.
        find_package(Git QUIET)
        set(EVENGINE_TP_DESC "unknown" CACHE INTERNAL "Third-party version for build info")
        set(_eve_tp_version_file "${_eve_prebuilt_tp}/third-party-version.txt")
        if(EXISTS "${_eve_tp_version_file}")
            file(STRINGS "${_eve_tp_version_file}" _eve_tp_version_lines)
            set(_eve_tp_aggregate "")
            set(_eve_tp_built "")
            foreach(_eve_tp_line IN LISTS _eve_tp_version_lines)
                if(_eve_tp_line MATCHES "^aggregate=(.*)$")
                    set(_eve_tp_aggregate "${CMAKE_MATCH_1}")
                elseif(_eve_tp_line MATCHES "^built=(.*)$")
                    set(_eve_tp_built "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            if(_eve_tp_aggregate)
                string(SUBSTRING "${_eve_tp_aggregate}" 0 12 _eve_tp_agg_short)
                set(EVENGINE_TP_DESC "prebuilt@${_eve_tp_agg_short}" CACHE INTERNAL
                    "Third-party version for build info" FORCE)
                message(STATUS "third-party prebuilt ${_eve_prebuilt_tp}: aggregate ${_eve_tp_aggregate} (built ${_eve_tp_built})")
            else()
                message(WARNING "third-party-version.txt has no aggregate= line: ${_eve_tp_version_file}")
            endif()
            if(EXISTS "${CMAKE_SOURCE_DIR}/third-party/.git" AND GIT_EXECUTABLE)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/third-party"
                    RESULT_VARIABLE _eve_tp_src_result
                    OUTPUT_VARIABLE _eve_tp_src_head
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(_eve_tp_src_result EQUAL 0 AND _eve_tp_aggregate
                   AND NOT _eve_tp_src_head STREQUAL _eve_tp_aggregate)
                    message(FATAL_ERROR
                        "third-party mismatch: prebuilt tree ${_eve_prebuilt_tp} was built from aggregate\n"
                        "  ${_eve_tp_aggregate}\n"
                        "but the source checkout third-party/ is at\n"
                        "  ${_eve_tp_src_head}\n"
                        "The engine would compile against new headers but link old prebuilt libraries.\n"
                        "Sync the checkout to the pinned commit and reinstall, or point\n"
                        "EVENGINE_THIRD_PARTY_BINARY_DIR at a prebuilt tree matching the source.")
                endif()
            endif()
        else()
            message(WARNING
                "prebuilt third-party tree has no ${_eve_tp_version_file}.\n"
                "Run 'make reinstall/third-party/<plat>' once to stamp the git version into the tree.")
        endif()
        # Preserve the dependency target expected by create_module(), but give it no
        # configure/build/install commands so the prebuilt tree can remain read-only.
        add_custom_target(third-party)
        add_custom_target(deps DEPENDS third-party)
        return()
    endif()

    # EVEngine's database module uses POCO Data + SQLite. The third-party
    # aggregate already defaults ENABLE_DATA / ENABLE_DATA_SQLITE to ON (pinned
    # by GIT_TAG below), so no configure-time edits are needed for it.
    #
    # Normal generation for assets without authored normals: medialoader maps
    # generateNormalsIfMissing to aiProcess_GenNormals (per-face, flat shading).
    # A tracked patch (cmake/patches/medialoader-smooth-normals.patch) switches
    # it to aiProcess_GenSmoothNormals, so sphere-like models shade smoothly
    # while hard edges (default smoothing angle 80 degrees) keep seams. The
    # patch is applied by the third-party ExternalProject's PATCH_COMMAND at
    # build time — never by mutating the checkout during configure.
    check_third_party_project(third-party "https://github.com/EVEngine/third-party")

    ADD_CUSTOM_TARGET(deps DEPENDS third-party)
    # The source checkout was verified against the pin by
    # ensure_third_party_checkout(); report it in the generated BuildInfo.h.
    string(SUBSTRING "${EVENGINE_THIRD_PARTY_PIN}" 0 12 _eve_tp_pin_short)
    set(EVENGINE_TP_DESC "source@${_eve_tp_pin_short}" CACHE INTERNAL
        "Third-party version for build info" FORCE)
endfunction()


# Resolve the WebGPU headers / library for BUILD_PLATFORM=webgpu.
#   * Emscripten (browser): webgpu.h / webgpu_cpp.h ship with the emsdk and are
#     already on emcc's include path; no link target is needed.
#   * Native desktop: Google Dawn is fetched via FetchContent and linked through
#     Dawn's native WebGPU target (set into EVENGINE_WEBGPU_LIB).
function(load_webgpu_backend)
    if(NOT EVENGINE_BUILD_HOST OR NOT BUILD_PLATFORM STREQUAL "webgpu")
        set(EVENGINE_WEBGPU_LIB "" CACHE INTERNAL "WebGPU link target (unused)")
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(STATUS "WebGPU: using emsdk headers (no Dawn build needed)")
        set(EVENGINE_WEBGPU_LIB "" CACHE INTERNAL "WebGPU link target (unused on Emscripten)")
        return()
    endif()

    message(STATUS "WebGPU (native): fetching Google Dawn")
    include(FetchContent)
    set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_WGPU_TESTS OFF CACHE BOOL "" FORCE)
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_NULL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
    set(DAWN_USE_XCODE OFF CACHE BOOL "" FORCE)
    if(WIN32)
        # Dawn's generic loader combines LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR with
        # bare system DLL names, which LoadLibraryEx rejects with ERROR 87.
        # Select its dedicated System32 path for Vulkan and D3D compiler DLLs.
        set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
    endif()

    # Pinned Dawn commit (2026-08-24 main HEAD) so native WebGPU builds stay
    # reproducible; override with -DEVENGINE_DAWN_TAG=<commit> if needed.
    set(EVENGINE_DAWN_TAG "521035d96861a33eadc06ebff7985add6ac7fdf6" CACHE STRING
        "Dawn commit/tag to fetch for the native WebGPU backend (pin for reproducible builds)")
    # A shallow git clone cannot reliably checkout an arbitrary fixed SHA once
    # main advances beyond it. GitHub's immutable commit archive preserves the
    # reproducible pin without downloading Dawn's complete history.
    FetchContent_Declare(dawn
        URL "https://github.com/google/dawn/archive/${EVENGINE_DAWN_TAG}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(dawn)

    if(TARGET dawn::webgpu_dawn)
        set(_eve_dawn_target dawn::webgpu_dawn)
    elseif(TARGET dawn::dawn)
        set(_eve_dawn_target dawn::dawn)
    else()
        message(FATAL_ERROR "Dawn fetch did not produce a native WebGPU target")
    endif()
    # Dawn's webgpu.h / webgpu_cpp.h live under src/ in the checkout; expose
    # them to every module target (engine, eve_imgui, gpgpu/webgpu, ...).
    target_include_directories(eve_engine_includes INTERFACE
        "${dawn_SOURCE_DIR}/include"
        "${dawn_SOURCE_DIR}/src"
        "${dawn_BINARY_DIR}/gen/include")
    if(TARGET dawncpp_headers)
        add_dependencies(eve_engine_includes dawncpp_headers)
    endif()
    if(TARGET dawn_headers)
        add_dependencies(eve_engine_includes dawn_headers)
    endif()
    if(TARGET emdawnwebgpu_headers_gen)
        add_dependencies(eve_engine_includes emdawnwebgpu_headers_gen)
    endif()
    set(EVENGINE_WEBGPU_LIB ${_eve_dawn_target} CACHE INTERNAL "WebGPU link target")
    message(STATUS "WebGPU (native): Dawn ready, link target = ${EVENGINE_WEBGPU_LIB}")
endfunction()

main()
