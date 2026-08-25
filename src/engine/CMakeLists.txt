create_module(EVCommon common)
eve_module_enabled(devtools _has_devtools)
if(_has_devtools)
    create_module(EVDevTools devtools)
    # Snapshot / DAP pull Poco JSON+Net; disable Windows #pragma lib auto-link
    # (would request PocoFoundationd.lib instead of our *mdd postfix).
    # NOMINMAX: Poco headers pull in windows.h; without it the min/max macros
    # break later includes such as CLI11 in cmdline/Run.cpp.
    target_compile_definitions(EVDevTools PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
endif()
create_module(EVCmdLine cmdline)
# DevTool.hpp (pulled in by cmdline/Run.cpp) includes Poco JSON/Net headers;
# keep the same no-auto-link contract so the debug Poco libs (*mdd) are used.
target_compile_definitions(EVCmdLine PUBLIC POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/config.h.in common/config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/common/BuildInfo.h.in common/BuildInfo.h)

set(EVENGINE_IOS_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS/iPadOS deployment target")

if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    if(NOT EVENGINE_ANDROID_TEST_APP)
        # SDLActivity loads libmain.so and calls SDL_main (via SDL_main.h macro on main).
        # In Android test-app mode the shared `main` library is built by test/CMakeLists.txt
        # from the zeroerr unit-test runner instead of the engine main.cpp.
        add_library(main SHARED main.cpp)
    endif()
    set(EVENGINE_NATIVE_TARGET main CACHE INTERNAL "Host target for SDK install")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    # UIKit SDL entry: MACOSX_BUNDLE .app with SDL_main.
    if(NOT EVENGINE_IOS_TEST_APP)
        add_executable(eve MACOSX_BUNDLE main.cpp)
    else()
        # iOS test app: the zeroerr runner (test/main.cpp + test sources) is
        # attached to this target from test/CMakeLists.txt. The bundle
        # properties below still apply so the .app ships signed and launches
        # like the game shell.
        add_executable(eve MACOSX_BUNDLE)
    endif()
    set(EVENGINE_NATIVE_TARGET eve CACHE INTERNAL "Host target for SDK install")
else()
    # Desktop + webgpu (native Dawn and Emscripten/WASM) produce `eve`.
    add_executable(eve main.cpp)
    set(EVENGINE_NATIVE_TARGET eve CACHE INTERNAL "Host target for SDK install")
endif()

# Crash-backtrace support (backward-cpp from EVEngine/third-party, MIT). The
# host binary installs an unhandled-exception handler (main.cpp) that prints a
# symbolized stack trace on crash; it is a host-only tool, not a game module.
if(WIN32)
    add_library(EVBacktrace INTERFACE)
    # backward.hpp lives in the third-party install; get its include dirs
    # through the shared include target instead of global include_directories.
    target_link_libraries(EVBacktrace INTERFACE eve_engine_includes)
    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        # Prebuilt-install mode: point at the install tree directly.
        target_include_directories(EVBacktrace INTERFACE ${EVENGINE_THIRD_PARTY_BINARY_DIR}/include)
        target_link_libraries(EVBacktrace INTERFACE
            ${EVENGINE_THIRD_PARTY_BINARY_DIR}/lib/backward$<$<CONFIG:Debug>:mdd>$<$<NOT:$<CONFIG:Debug>>:md>.lib)
    else()
        # ExternalProject mode (CI): the root CMakeLists already added the
        # third-party install include/lib dirs to the global search paths.
        target_link_libraries(EVBacktrace INTERFACE
            backward$<$<CONFIG:Debug>:mdd>$<$<NOT:$<CONFIG:Debug>>:md>)
    endif()
endif()

# Engine entry-point sources include common/config.h / cmdline/*.h directly;
# link the shared include set explicitly (also propagates the third-party
# link directories needed by the bare-name ${ThirdParty} list).
if(TARGET ${EVENGINE_NATIVE_TARGET})
    # Plain signature: the target's other links use the plain form.
    target_link_libraries(${EVENGINE_NATIVE_TARGET} eve_engine_includes)
endif()

# Platform system libraries, shared with the unit-test runner
# (see cmake/system_libraries.cmake).
eve_append_system_libraries(SystemLibraries)

# Third-party link list, derived from the module manifest: only the libraries
# the enabled modules actually need, in the canonical order GNU ld requires.
# See cmake/thirdparty_libs.cmake for the group -> link-name mapping.
eve_thirdparty_libs(_eve_tp_libs ${EVE_THIRDPARTY_GROUPS})
set(ThirdParty ${_eve_tp_libs} ${SystemLibraries})
message(STATUS "Third-party groups: ${EVE_THIRDPARTY_GROUPS}")

# Module link list, derived from the module manifest; EVCommon, EVCmdLine and
# EVDevTools are declared there too, so only the embedded scripts are added here.
set(EVELIBS EVScripts ${EVE_MODULE_LIBS})
if(_has_devtools)
    # DevTools ImGui host panels (AI + console) compile into the host binary --
    # not EVDevTools, which avoids imgui.h to protect MSVC export limits.
    target_sources(${EVENGINE_NATIVE_TARGET} PRIVATE devtools/ImGuiHostPanels.cpp)
endif()
eve_module_enabled(ui _has_ui)
if(_has_ui)
    list(APPEND EVELIBS eve_imgui)
endif()

if(EVENGINE_PLATFORM_LIB)
    list(APPEND EVELIBS ${EVENGINE_PLATFORM_LIB})
endif()

# Host TUs (main.cpp, ImGuiHostPanels.cpp) can pull Poco headers through
# engine includes; keep the same no-auto-link contract as the modules.
if(WIN32 AND TARGET ${EVENGINE_NATIVE_TARGET})
    target_compile_definitions(${EVENGINE_NATIVE_TARGET} PRIVATE POCO_STATIC POCO_NO_AUTOMATIC_LIBS NOMINMAX)
    if(MSVC)
        # SDL_WINDOW_ALLOW_HIGHDPI does not make an SDL 2.0 process DPI-aware
        # by itself. Without this manifest Windows bitmap-scales the complete
        # framebuffer (commonly by 150%), making ImGui text and hairlines soft.
        target_sources(${EVENGINE_NATIVE_TARGET} PRIVATE eve.rc)
        # eve.rc already contains resource #1, so disable MSVC's generated
        # manifest resource instead of merging two resources with the same ID.
        target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE "/MANIFEST:NO")
    endif()
    # Plain form: the host target's other third-party libs are linked plainly.
    target_link_libraries(${EVENGINE_NATIVE_TARGET} EVBacktrace)
    # Plugins link against eve.lib and need the host's full symbol surface
    # (Squirrel C API, ssq, engine internals), not just EVENGINE_API-marked
    # classes. Export every symbol from eve.exe so eve.lib satisfies plugins.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Copy the generated build metadata to the build root after every link so
# `build/<plat>/build-info.txt` records exactly which engine git commit and
# third-party version this build corresponds to (same data as `eve --version`).
if(TARGET ${EVENGINE_NATIVE_TARGET})
    add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/common/BuildInfo.h
            ${CMAKE_BINARY_DIR}/build-info.txt
        VERBATIM)
endif()

if(NOT EVENGINE_ANDROID_TEST_APP)
  if(CMAKE_GENERATOR MATCHES "Xcode")
    # create_module() emits STATIC libraries here, so the linker would drop the
    # translation units whose only job is to self-register a module or a
    # subcommand at static-init time. Pull the archives in whole.
    set(_eve_whole_archive_libs "")
    foreach(_eve_lib IN LISTS EVELIBS)
        list(APPEND _eve_whole_archive_libs "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_eve_lib}>")
    endforeach()
    target_link_libraries(${EVENGINE_NATIVE_TARGET} ${_eve_whole_archive_libs} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  else()
    target_link_libraries(${EVENGINE_NATIVE_TARGET} ${EVELIBS} zeroerr ${ThirdParty} ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        if(NOT EVENGINE_MOLTENVK_FRAMEWORK_DIR)
            message(FATAL_ERROR "EVENGINE_MOLTENVK_FRAMEWORK_DIR is not set (MoltenVK xcframework)")
        endif()
        target_link_libraries(${EVENGINE_NATIVE_TARGET}
            "-F${EVENGINE_MOLTENVK_FRAMEWORK_DIR}"
            "-framework MoltenVK"
        )
        target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE
            "LINKER:-rpath,@executable_path/Frameworks"
        )
    elseif(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "static")
        if(NOT EVENGINE_MOLTENVK_STATIC_LIB OR NOT EXISTS "${EVENGINE_MOLTENVK_STATIC_LIB}")
            message(FATAL_ERROR "EVENGINE_MOLTENVK_STATIC_LIB missing: ${EVENGINE_MOLTENVK_STATIC_LIB}")
        endif()
        # macOS/lib MoltenVK.xcframework ships ios-arm64 as libMoltenVK.a (CI layout).
        target_link_libraries(${EVENGINE_NATIVE_TARGET} "${EVENGINE_MOLTENVK_STATIC_LIB}")
    else()
        message(FATAL_ERROR "EVENGINE_MOLTENVK_LINK_MODE unset/unknown (got: '${EVENGINE_MOLTENVK_LINK_MODE}')")
    endif()

    set(EVENGINE_IOS_BUNDLE_ID "com.evengine.example" CACHE STRING "iOS/iPadOS bundle identifier")
    set(EVENGINE_IOS_TEST_BUNDLE_ID "com.evengine.example.test" CACHE STRING
        "iOS test app bundle identifier (side-by-side with the game app)")
    if(EVENGINE_IOS_TEST_APP)
        set(_eve_ios_bundle_id "${EVENGINE_IOS_TEST_BUNDLE_ID}")
        set(_eve_ios_bundle_name "EVEngineTest")
        set(_eve_ios_display_name "EVEngine Test")
    else()
        set(_eve_ios_bundle_id "${EVENGINE_IOS_BUNDLE_ID}")
        set(_eve_ios_bundle_name "EVEngine")
        set(_eve_ios_display_name "EVEngine")
    endif()
    set(EVENGINE_IOS_DISPLAY_NAME "${_eve_ios_display_name}")
    set(_eve_ios_team "$ENV{IOS_DEVELOPMENT_TEAM}")
    if(NOT _eve_ios_team)
        set(_eve_ios_team "")
    endif()
    set(IOS_DEVELOPMENT_TEAM "${_eve_ios_team}" CACHE STRING "Apple Development Team ID for code signing")

    set(_eve_ios_rpath "")
    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        set(_eve_ios_rpath "@executable_path/Frameworks")
    endif()
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "${_eve_ios_bundle_id}"
        MACOSX_BUNDLE_BUNDLE_NAME "${_eve_ios_bundle_name}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${_eve_ios_bundle_id}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${EVENGINE_IOS_DEPLOYMENT_TARGET}"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO
        XCODE_ATTRIBUTE_ENABLE_BITCODE NO
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "${_eve_ios_rpath}"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    )

    if(EVENGINE_IOS_TEST_APP)
        # Bundle the zeroerr suite's test/ + examples/ trees so the runner can
        # stage them into a writable location on first launch (mirrors the
        # Android test app's assets/test + assets/examples unpacking).
        add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
            # Wipe first so assets removed from the tree do not linger in the bundle.
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/test"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/examples"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/test"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/examples"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/test" "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/test"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/examples"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard"
            "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/LaunchScreen.storyboard"
            COMMENT "Bundle iOS test assets (test/ + examples/) + LaunchScreen into EVEngine.app"
        )
    else()
        # Bundle game assets under Resources/game (iOS resourcePath == .app root).
        # Default "demo" = shell without main.nut → load.nut runs embedded eve.demoScript
        # (meteor defense + particles). "example" copies example/ (box + fire.json).
        set(EVENGINE_IOS_GAME "demo" CACHE STRING "iOS bundled game: demo | example | absolute path")
        if(EVENGINE_IOS_GAME STREQUAL "example")
            set(_eve_game_src "${CMAKE_SOURCE_DIR}/example")
        elseif(EVENGINE_IOS_GAME STREQUAL "demo")
            set(_eve_game_src "${CMAKE_SOURCE_DIR}/platform/ios/game-shell")
        elseif(IS_DIRECTORY "${EVENGINE_IOS_GAME}")
            set(_eve_game_src "${EVENGINE_IOS_GAME}")
        else()
            message(FATAL_ERROR "EVENGINE_IOS_GAME must be 'demo', 'example', or an existing directory (got: ${EVENGINE_IOS_GAME})")
        endif()
        add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
            # Wipe first so a previous example/main.nut cannot shadow demoScript.
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_eve_game_src}" "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/game"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard"
            "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/LaunchScreen.storyboard"
            COMMENT "Bundle iOS game (${EVENGINE_IOS_GAME}) + LaunchScreen into EVEngine.app"
        )
    endif()

    if(EVENGINE_MOLTENVK_LINK_MODE STREQUAL "framework")
        # Embed MoltenVK.framework so dlsym(RTLD_DEFAULT) / rpath resolve at runtime.
        # The helper also signs the framework; nested frameworks need their own signature.
        set(_eve_mvk_fw "${EVENGINE_MOLTENVK_FRAMEWORK_DIR}/MoltenVK.framework")
        add_custom_command(TARGET ${EVENGINE_NATIVE_TARGET} POST_BUILD
            COMMAND /bin/sh "${CMAKE_SOURCE_DIR}/cmake/ios_embed_moltenvk.sh"
            "${_eve_mvk_fw}"
            "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>"
            COMMENT "Embed and sign MoltenVK.framework into EVEngine.app"
        )
    endif()
endif()

# ---- Emscripten (browser WebGPU / WASM) ----
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # The engine's blocking script loop (load.nut) runs on a pthread; browser
    # WebGPU calls are proxied to the main thread by Emscripten.
    target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE
        # main() runs on the browser main thread so emdawnwebgpu can reach the
        # DOM canvas directly (PROXY_TO_PTHREAD would run main() on a worker
        # with no DOM, and the OffscreenCanvas transfer path hits
        # emscripten_proxy_async failures). pthreads stay available for the
        # Thread module; ASYNCIFY lets the blocking main loop (emscripten_sleep)
        # yield to the browser event loop.
        -sUSE_PTHREADS=1
        -sALLOW_MEMORY_GROWTH=1
        # Pre-allocate the worker pool so Emscripten does not print "thread pool
        # is exhausted" (it warns whenever a new worker is spawned while the pool
        # is empty). 4 covers the engine's ThreadPool default (capped at 4 on
        # Emscripten) and any library init.
        -sPTHREAD_POOL_SIZE=4
        -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1
        -sASSERTIONS=1
        -sFETCH=1
        # The playground shell calls these helpers directly on Module.
        # Emscripten 6 no longer auto-adds _main when EXPORTED_FUNCTIONS is set.
        -sEXPORTED_FUNCTIONS=_main,_malloc,_free
        -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,UTF8ToString,HEAPU8
        # C++ exceptions can be thrown through ASYNCIFY-wrapped frames (e.g. a
        # module expose() failing); keep JS-side catching enabled so the abort
        # helper is compiled instead of asserting on the first __cxa_throw.
        -sNO_DISABLE_EXCEPTION_CATCHING=1
        # The WebGPU backend spin-waits for requestAdapter/requestDevice with
        # emscripten_sleep(0); on the main thread that requires ASYNCIFY.
        -sASYNCIFY=1
        # Frame readback (saveFramePng / getPixel) sleeps inside a deep native
        # call chain (JS -> Squirrel -> Graphics); the default asyncify stack
        # overflows ("unreachable") and aborts the runtime.
        -sASYNCIFY_STACK_SIZE=32768
        # The same chain (Squirrel VM + readback + LodePNG/zlib encode) exceeds
        # the 64 KiB default wasm stack; give the main thread room to breathe.
        -sTOTAL_STACK=1048576
    )

    set(EVENGINE_WEBGPU_GAME_DIR "${CMAKE_SOURCE_DIR}/platform/webgpu/game-shell" CACHE PATH
        "Directory preloaded as /game on the browser (default: platform/webgpu/game-shell)")

    set(_eve_web_shell "${CMAKE_SOURCE_DIR}/platform/webgpu/shell.html")
    if(EXISTS "${_eve_web_shell}")
        target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE --shell-file "${_eve_web_shell}")
        # Link to eve.html (not the default eve.js) so --shell-file is applied
        # and the served page is regenerated on every build. Emscripten emits
        # eve.js / eve.wasm / eve.data alongside it.
        set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES SUFFIX ".html")
        # Track the shell as a link input so ninja relinks when it changes.
        set_property(TARGET ${EVENGINE_NATIVE_TARGET} APPEND PROPERTY
            LINK_DEPENDS "${_eve_web_shell}")
    endif()

    # --preload-file mounts the game assets at /game so getGameDirectory()
    # ("/game") resolves directly against the preloaded VFS.
    if(IS_DIRECTORY "${EVENGINE_WEBGPU_GAME_DIR}")
        # --preload-file takes the source path relative to the CMake working
        # directory; use the absolute game-shell dir so the wasm data embeds
        # the assets directly at /game without a staging copy.
        target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE
            "--preload-file" "${EVENGINE_WEBGPU_GAME_DIR}@/game"
        )
        # Track the preloaded assets as link inputs so demo/asset edits
        # regenerate eve.data instead of shipping a stale VFS.
        file(GLOB_RECURSE _eve_game_files "${EVENGINE_WEBGPU_GAME_DIR}/*")
        set_property(TARGET ${EVENGINE_NATIVE_TARGET} APPEND PROPERTY
            LINK_DEPENDS ${_eve_game_files})
    endif()
endif()

if(NOT EVENGINE_ANDROID_TEST_APP)
# Plugins resolve ModuleManager / ModuleRegister symbols from the host process.
# Export surface is EVENGINE_API (__declspec(dllexport)) only — do not enable
# WINDOWS_EXPORT_ALL_SYMBOLS: it pulls Poco/ImGui template instantiations into
# the .def and hits MSVC LNK1189 (65535 export limit) as the engine grows.
target_compile_definitions(${EVENGINE_NATIVE_TARGET} PRIVATE EVENGINE_BUILDING_HOST=1)
if(WIN32)
    # ENABLE_EXPORTS so plugins can link/import host symbols.
    # Do NOT enable WINDOWS_EXPORT_ALL_SYMBOLS: MSVC hits LNK1189 (65535
    # objects/exports) once the engine grows. Plugins only need EVENGINE_API
    # (__declspec(dllexport)) symbols such as ModuleManager / ModuleRegister.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        ENABLE_EXPORTS ON
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
elseif(APPLE)
    # macOS + iOS: Apple ld rejects GNU --export-dynamic.
    target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE "LINKER:-export_dynamic")
elseif(UNIX AND NOT ANDROID AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Android")
       AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
    target_link_options(${EVENGINE_NATIVE_TARGET} PRIVATE "LINKER:--export-dynamic")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # WASM: dlopen/plugins are not supported; no symbol export flags needed.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )
elseif(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    # libmain.so: keep default-visible EVENGINE_API symbols for dlopened plugins.
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )
endif()
endif()
