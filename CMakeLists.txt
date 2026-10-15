cmake_minimum_required(VERSION 3.21)

# Keep Windows/Ninja compiler invocations below cmd.exe's command-line limit
# (long include graphs and per-target flags) by letting Ninja pass flags
# through response files.
if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL
        "Use response files for Windows Ninja compile and link commands" FORCE)
endif()

set(EVENGINE_BUILD_PLATFORM "" CACHE STRING
    "EVEngine target platform override; unlike legacy BUILD_PLATFORM this cannot collide with dependencies")

set(EVENGINE_COMPILER_CACHE "AUTO" CACHE STRING
    "Compiler cache mode: AUTO, SCCACHE, CCACHE_HARDLINK (Windows Ninja), or OFF")
set_property(CACHE EVENGINE_COMPILER_CACHE PROPERTY STRINGS AUTO SCCACHE CCACHE_HARDLINK OFF)
string(TOUPPER "${EVENGINE_COMPILER_CACHE}" _eve_compiler_cache_mode)
if(NOT _eve_compiler_cache_mode MATCHES "^(AUTO|SCCACHE|CCACHE_HARDLINK|OFF)$")
    message(FATAL_ERROR
        "EVENGINE_COMPILER_CACHE must be AUTO, SCCACHE, CCACHE_HARDLINK, or OFF "
        "(got: ${EVENGINE_COMPILER_CACHE})")
endif()

set(_eve_sccache_enabled OFF)
set(_eve_ccache_enabled OFF)
if(_eve_compiler_cache_mode STREQUAL "CCACHE_HARDLINK")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ccache_hardlink.cmake")
    eve_prepare_ccache_hardlink()
    set(_eve_ccache_enabled ON)
    set(_eve_compiler_cache_status "ccache hardlinks (${EVENGINE_CCACHE_EXECUTABLE})")
elseif(_eve_compiler_cache_mode STREQUAL "AUTO" AND
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

if(_eve_ccache_enabled)
    eve_finish_ccache_hardlink()
endif()

if(WIN32 AND MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT _eve_ccache_enabled)
    # sccache (and any other launcher) CreateProcess()es cl.exe with a pipe;
    # localized cl then writes ACP /showIncludes (GBK) while Ninja's
    # msvc_deps_prefix is UTF-8, so every header leaks into the build log.
    # Route compiles through cmake/msvc-cl.cmd so cl inherits CP65001.
    set(_eve_msvc_cl_wrap "${CMAKE_SOURCE_DIR}/cmake/msvc-cl.cmd")
    foreach(_eve_lang IN ITEMS C CXX)
        get_filename_component(_eve_comp_name "${CMAKE_${_eve_lang}_COMPILER}" NAME)
        if(_eve_comp_name MATCHES "^cl(\\.exe)?$")
            set(CMAKE_${_eve_lang}_COMPILER "${_eve_msvc_cl_wrap}"
                CACHE FILEPATH "MSVC cl UTF-8 /showIncludes wrapper" FORCE)
        endif()
    endforeach()
endif()

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
set(EVENGINE_MINOR_VERSION "6")
set(EVENGINE_PATCH_VERSION "0")
set(EVENGINE_DEV_VERSION "-dev")

set(EVENGINE_VERSION "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}${EVENGINE_DEV_VERSION}")

option(BUILD_TESTING "enable testing" ON)
option(EVENGINE_BUILD_HOST
    "Build the interactive/native eve host (disabled by core/headless/server profiles)" ON)
option(EVENGINE_BUILD_PROFILE_CHECKS
    "Build independent public-header and optional-capability profile checks" OFF)
option(EVENGINE_BUILD_PIXELWORLD_BENCHMARK
    "Build the renderer-independent PixelWorld million-cell benchmark" OFF)
option(EVENGINE_BUILD_RENDER_BENCHMARK
    "Build the windowed engine rendering performance benchmark" OFF)
option(EVENGINE_BUILD_DEMO "Build optional Demo module (procedural demo assets + default demo.nut)" ON)
option(EVENGINE_VULKAN_USE_VMA
    "Use the bundled Vulkan Memory Allocator for VKBuilder buffers" ON)
# Per-module shared libraries. Modules are OBJECT libraries by default, so every
# consumer (eve, each unit_test_<domain>) absorbs all engine objects and pays the
# engine's debug information again: a per-domain test PDB measured 1.2 GB of
# which only ~0.8 MB per test file is the test code. As SHARED, the engine debug
# info stays in one PDB per module and consumers link import libraries.
#
# A single whole-engine DLL is not an option: it would need
# WINDOWS_EXPORT_ALL_SYMBOLS, which the engine already exceeds (MSVC LNK1189,
# 65535 exports; see src/engine/CMakeLists.txt). Per module the export surface is
# only that module's own symbols, well inside the limit.
#
# Policy: development builds take the dynamic route (SHARED) so that iterating on
# a domain test relinks a test executable instead of the whole engine; release and
# SDK builds pass OBJECT explicitly (Makefile WIN32_CMAKE_ARGS) so the shipped
# artifact is one exe with no engine DLLs beside it. All 30 test domains link and
# run under SHARED (see
# docs/dev/superpowers/specs/2026-08-18-test-suite-optimization.md sections
# 7.7-7.19), so SHARED is the default here and OBJECT is the opt-in: anything that
# ships an artifact must pass -DEVENGINE_MODULE_LINKAGE=OBJECT.
#
# The dynamic route is a desktop development convenience -- one library per link
# group so iterating on a domain test relinks a test executable instead of the
# whole engine. Mobile and WebAssembly artifacts ship as a single module or app
# bundle and cannot be told where to find seven group libraries, so those
# platforms keep the archive-only route whatever the default is.
#
# macOS is in that list for a concrete reason, not by policy: its platform support
# library (platform/macosx -> EVPlatformMacOSX) is linked into the host only, so
# libEVFoundation.dylib cannot resolve eve::macosx::getExecutablePath(), which
# eve::filesystem::Filesystem::getExecutablePath() calls. The dynamic route there
# needs that library linked into the group that consumes it (or the symbol moved
# behind an interface); until then macOS builds the archive route and is
# unaffected by the desktop default. Windows and Linux are the verified dynamic
# platforms (see the spec section 7.25 readings).
if(ANDROID OR IOS OR EMSCRIPTEN
   OR CMAKE_SYSTEM_NAME STREQUAL "iOS"
   OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
   OR BUILD_PLATFORM STREQUAL "android"
   OR BUILD_PLATFORM STREQUAL "ios"
   OR BUILD_PLATFORM STREQUAL "macosx"
   OR BUILD_PLATFORM STREQUAL "webgpu")
    set(_eve_module_linkage_default "OBJECT")
else()
    set(_eve_module_linkage_default "SHARED")
endif()
set(EVENGINE_MODULE_LINKAGE "${_eve_module_linkage_default}" CACHE STRING
    "How each manifest module is built: SHARED (desktop default; one library per link group) or OBJECT (release/SDK/mobile/WASM: one self-contained artifact)")
set_property(CACHE EVENGINE_MODULE_LINKAGE PROPERTY STRINGS OBJECT SHARED)
if(NOT EVENGINE_MODULE_LINKAGE MATCHES "^(OBJECT|SHARED)$")
    message(FATAL_ERROR
        "EVENGINE_MODULE_LINKAGE must be OBJECT or SHARED (got: ${EVENGINE_MODULE_LINKAGE})")
endif()

# In the dynamic configuration the executables link with -z nocopyreloc
# (cmake/link_groups.cmake) so that external data references stay GOT-based and read
# the live value in the library that owns it. That requires every object to be
# *-fPIC*, not merely *-fPIE*: with -fPIE the compiler still emits PC32 relocations
# for data it expects the linker to copy into the executable, and once the linker is
# told not to copy, the ELF loader rejects those relocations at startup ("Symbol
# '_ZSt4cerr' causes overflow in R_X86_64_PC32 relocation"). The group libraries
# already set POSITION_INDEPENDENT_CODE; this covers the executables and the
# in-tree archives they link. MSVC ignores the flag.
if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED" AND NOT MSVC)
    add_compile_options(-fPIC)
endif()
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
option(EVENGINE_BUILD_JOLT "Enable the Jolt rigid-body provider" ON)
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
     Third-party targets are not affected.
     Default OFF in CMake; the Makefile turns it ON for win32-debug and
     linux-debug so local builds match CI (debug SDK aliases reuse those
     trees). Release, macOS debug, Android, and iOS stay OFF."
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


# Build order: platform -> manifest/profile -> dependencies -> engine modules
# -> link groups -> consumers -> SDK installation. Keep target creation here;
# helper implementations below are loaded before main() is called.
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
    include(${CMAKE_SOURCE_DIR}/cmake/image_webp.cmake)
    load_image_webp()
    load_asset_zstd()
    load_asset_utf8proc()
    load_webgpu_backend()
    set_include()
    eve_prepare_simplesquirrel_headers()
    eve_configure_ipo()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    # cl.exe already sees the STL/UCRT via %INCLUDE% from cmake/with-msvc.cmd.
    # Do not pass those directories as extra -I flags: MSVC /showIncludes then
    # lists every system header, and a truncated or oversized record makes
    # Ninja print "premature end of file; recovering" on .ninja_deps and
    # rebuild every deps=msvc object. clangd should use --query-driver=**/cl.exe
    # instead of relying on those -I flags in compile_commands.json.
    if(MSVC AND (NOT DEFINED ENV{INCLUDE} OR "$ENV{INCLUDE}" STREQUAL ""))
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
    # Enabled by CI and by local `make build/win32-debug` / `make build/linux-debug`
    # (see DEBUG_STRICT_WARNINGS_FLAG in the root Makefile). Opt out with:
    #   make build/win32-debug CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_STRICT_WARNINGS=OFF
    eve_configure_strict_warnings(eve_engine_includes)

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
        # Poco's platform header still recognizes the legacy EMSCRIPTEN token,
        # while current em++ defines __EMSCRIPTEN__. Keep the third-party
        # architecture detection explicit for modules that consume Poco on web.
        add_compile_definitions(EMSCRIPTEN)
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
    include(cmake/zeroerr_compiled_diagnostics.cmake)
    add_subdirectory(external/zeroerr)
    eve_enable_zeroerr_compiled_diagnostics()
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
    # Do not stamp wall-clock time on every configure: that rewrites
    # BuildInfo.h whenever ninja re-runs CMake, even if no sources changed.
    set(_eve_build_id "${EVENGINE_GIT_COMMIT}:${EVENGINE_GIT_DIRTY}")
    if(NOT DEFINED EVENGINE_BUILD_TIME OR NOT _eve_build_id STREQUAL "${EVENGINE_BUILD_ID}")
        string(TIMESTAMP EVENGINE_BUILD_TIME "%Y-%m-%d %H:%M:%S %z")
        set(EVENGINE_BUILD_TIME "${EVENGINE_BUILD_TIME}" CACHE INTERNAL "Frozen until git identity changes")
        set(EVENGINE_BUILD_ID "${_eve_build_id}" CACHE INTERNAL "Git identity for BuildInfo.h")
    endif()
    if(NOT EVENGINE_TP_DESC)
        set(EVENGINE_TP_DESC "unknown")
    endif()

    # external/ECS.hpp is patched the same way third-party is, and for the same
    # reason: `ecs::default_table()` is an inline function whose function-local
    # static is duplicated by every image once the engine is split into link-group
    # DLLs, so a domain module and its caller would address different default
    # Tables. The patch forwards to an exported owner compiled into the foundation
    # link unit (src/engine/common/EcsDefaultTable.cpp); the OBJECT / static route
    # and the release SDK are unaffected.
    #
    # It runs at configure time because the header is compiled by module
    # translation units and nothing orders a patch step in front of them. Skipped
    # when the submodule is absent (profiles and tools that do not use ECS), and
    # skipped without touching anything when the checkout already carries it.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp/src/ECS.hpp")
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/ecs-shared-default-table.patch
                -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/external/ECS.hpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_third_party.cmake
            RESULT_VARIABLE _eve_ecs_patch_result)
        if(NOT _eve_ecs_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Patching external/ECS.hpp failed (exit ${_eve_ecs_patch_result}). "
                "The vendored submodule moved away from the pinned commit; "
                "update cmake/patches/ecs-shared-default-table.patch.")
        endif()
    endif()
    # set_include() generated the ECS overlay before the patch above. Refresh
    # it now so a clean checkout compiles the patched default-table API.
    eve_prepare_ecs_container_headers()

    add_subdirectory(src/engine)
    add_subdirectory(src/modules)

    # Group module objects after their targets exist and before consumers link
    # EVE_LINK_TARGETS (SHARED builds).
    include(${CMAKE_SOURCE_DIR}/cmake/link_groups.cmake)
    if(EVENGINE_BUILD_PIXELWORLD_BENCHMARK)
        add_subdirectory(benchmarks/pixelworld)
    endif()
    if(EVENGINE_BUILD_HOST AND EVENGINE_BUILD_RENDER_BENCHMARK)
        add_subdirectory(benchmarks/render)
    endif()
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

# Ensure third-party sources exist at configure time and sit on the pinned
# commit. ExternalProject only downloads during the build of target
# `third-party`, but modules (e.g. eve_imgui) list files under third-party/
# at configure time.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party_build.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/webgpu_backend.cmake)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/module_targets.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform_selection.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/asset_dependencies.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_options.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine_usage.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ecs_container_headers.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/simplesquirrel_headers.cmake")

main()

if(_eve_ccache_enabled)
    eve_attach_ccache_source_map("${CMAKE_SOURCE_DIR}")
endif()
