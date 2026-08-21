# EVEngine SDK install rules — one independent SDK per BUILD_PLATFORM (target publish platform).
# Usage:
#   cmake --install build/<plat>[-debug] --prefix dist/eve-sdk/<plat>[-debug]

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(EVENGINE_SDK_VERSION
    "${EVENGINE_MAJOR_VERSION}.${EVENGINE_MINOR_VERSION}.${EVENGINE_PATCH_VERSION}")

# ---- Host binary ----
if(ANDROID OR (CMAKE_SYSTEM_NAME STREQUAL "Android"))
    install(TARGETS ${EVENGINE_NATIVE_TARGET}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION lib
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EVENGINE_IOS)
    install(TARGETS ${EVENGINE_NATIVE_TARGET}
        BUNDLE DESTINATION bin
        RUNTIME DESTINATION bin
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    install(TARGETS ${EVENGINE_NATIVE_TARGET}
        RUNTIME DESTINATION bin
    )
    # The browser build produces eve.js / eve.wasm / eve.html next to the
    # runtime output; copy them plus the preloaded game into bin/.
    install(FILES
        "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/eve.js"
        "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/eve.wasm"
        "$<TARGET_FILE_DIR:${EVENGINE_NATIVE_TARGET}>/eve.html"
        DESTINATION bin
        OPTIONAL
    )
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/platform/webgpu/game-shell"
        DESTINATION bin
        OPTIONAL
    )
else()
    install(TARGETS ${EVENGINE_NATIVE_TARGET}
        RUNTIME DESTINATION bin
        # Windows import library for plugins linking against eve.exe exports
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )
endif()

# macOS: eve links several dynamic libraries (Vulkan loader from the SDK,
# zlib/PNG/... from the third-party tree). Bundle every *.dylib into the SDK
# and give eve an @loader_path rpath so the installed binary runs without the
# build environment. SDL2 and MoltenVK are linked statically.
if(BUILD_PLATFORM STREQUAL "macosx")
    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        set(_eve_mac_tp_lib "${EVENGINE_THIRD_PARTY_BINARY_DIR}/lib")
    else()
        set(_eve_mac_tp_lib "${CMAKE_SOURCE_DIR}/build/third-party-binary/macosx/lib")
    endif()
    install(CODE "
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/lib\")
        foreach(_eve_dylib_dir IN ITEMS \"\$ENV{VULKAN_SDK}/lib\" \"${_eve_mac_tp_lib}\")
            file(GLOB _eve_dylibs \"\${_eve_dylib_dir}/*.dylib\")
            foreach(_eve_dylib IN LISTS _eve_dylibs)
                file(COPY \"\${_eve_dylib}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/lib\")
            endforeach()
        endforeach()
    ")
    set_target_properties(${EVENGINE_NATIVE_TARGET} PROPERTIES
        INSTALL_RPATH "@loader_path/../lib;@loader_path"
    )
endif()

# ---- Public headers (engine common + module façades) ----
install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/engine/common/"
    DESTINATION include/eve/common
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)
install(FILES "${CMAKE_BINARY_DIR}/src/engine/common/config.h"
    DESTINATION include/eve/common
)

# Public module headers: export exactly the modules this build enabled, so the
# SDK's API surface always matches the target runtime. Single source of truth is
# cmake/module_manifest.cmake (EVE_ENABLED_MODULES); CORE modules (common /
# cmdline / devtools) live under src/engine and have no src/modules/<name> dir.
if(NOT DEFINED EVE_ENABLED_MODULES)
    # Defensive fallback when included outside the engine configure: every dir.
    file(GLOB _eve_all_module_dirs RELATIVE "${CMAKE_SOURCE_DIR}/src/modules"
        "${CMAKE_SOURCE_DIR}/src/modules/*")
    set(EVE_ENABLED_MODULES ${_eve_all_module_dirs})
endif()
foreach(_eve_mod IN LISTS EVE_ENABLED_MODULES)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/modules/${_eve_mod}")
        continue()
    endif()
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/modules/${_eve_mod}/"
        DESTINATION include/eve/${_eve_mod}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "sdl" EXCLUDE
            PATTERN "vulkan" EXCLUDE
            PATTERN "webgpu" EXCLUDE
            PATTERN "physfs" EXCLUDE
            PATTERN "openal" EXCLUDE
            PATTERN "imgui" EXCLUDE
            PATTERN "cppfs" EXCLUDE
            PATTERN "include_shim" EXCLUDE
    )
endforeach()

# ---- Licenses -----------------------------------------------------------
# Distribution must carry attribution for the engine AND every third-party
# component. Root licenses are required; vendored-source licenses under
# external/* and third-party/ are best-effort (both may be absent on machines
# that use a prebuilt third-party tree).
install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/LICENSE-COMMERCIAL"
    "${CMAKE_SOURCE_DIR}/LICENSE-OPENSOURCE"
    DESTINATION share/eve/licenses
)
foreach(_eve_lic_src IN ITEMS
    "${CMAKE_SOURCE_DIR}/external"
    "${CMAKE_SOURCE_DIR}/third-party"
)
    if(EXISTS "${_eve_lic_src}")
        install(DIRECTORY "${_eve_lic_src}/"
            DESTINATION share/eve/licenses
            FILES_MATCHING
                PATTERN "LICENSE*"
                PATTERN "LICENCE*"
                PATTERN "COPYING*"
                PATTERN "NOTICE*"
                PATTERN ".git" EXCLUDE
        )
    endif()
endforeach()

# ---- Reference example ----------------------------------------------------
# Ship a runnable reference game so SDK consumers can see the layout without
# cloning the repository. `eve run` in a folder with no main.nut still falls
# back to the embedded demo.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/basic"
    DESTINATION share/eve/examples
)

# ---- Target-platform packaging template only ----
set(_eve_plat_src "${CMAKE_SOURCE_DIR}/platform/${BUILD_PLATFORM}")
if(EXISTS "${_eve_plat_src}")
    if(BUILD_PLATFORM STREQUAL "android")
        install(DIRECTORY "${_eve_plat_src}/"
            DESTINATION platform
            PATTERN "apk/app/build" EXCLUDE
            PATTERN "apk/.gradle" EXCLUDE
            PATTERN "apk/build" EXCLUDE
            PATTERN "apk/local.properties" EXCLUDE
            PATTERN "apk/app/src/main/jniLibs" EXCLUDE
            PATTERN "apk/app/src/main/assets/game/*" EXCLUDE
            PATTERN "CMakeCache.txt" EXCLUDE
            PATTERN "*.o" EXCLUDE
            PATTERN "*.a" EXCLUDE
        )
        # Keep assets/game placeholder
        install(FILES "${CMAKE_SOURCE_DIR}/platform/android/apk/app/src/main/assets/game/.gitkeep"
            DESTINATION platform/apk/app/src/main/assets/game
            OPTIONAL
        )
    else()
        install(DIRECTORY "${_eve_plat_src}/"
            DESTINATION platform
            PATTERN "CMakeCache.txt" EXCLUDE
            PATTERN "*.o" EXCLUDE
            PATTERN "*.a" EXCLUDE
        )
    endif()
endif()

# ---- Android runtime shared libs needed to assemble an APK ----
if(BUILD_PLATFORM STREQUAL "android")
    if(EVENGINE_THIRD_PARTY_BINARY_DIR)
        set(_eve_tp_lib "${EVENGINE_THIRD_PARTY_BINARY_DIR}")
    else()
        set(_eve_tp_lib "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}")
    endif()
    if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_eve_tp_lib "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug")
    endif()
    if(EXISTS "${_eve_tp_lib}/lib/libSDL2.so")
        install(FILES "${_eve_tp_lib}/lib/libSDL2.so" DESTINATION lib)
    endif()
    if(EXISTS "${_eve_tp_lib}/lib/libhidapi.so")
        install(FILES "${_eve_tp_lib}/lib/libhidapi.so" DESTINATION lib)
    endif()
    # libc++_shared.so from NDK (best-effort; packaging docs note NDK fallback)
    if(DEFINED ANDROID_NDK AND EXISTS "${ANDROID_NDK}")
        file(GLOB _eve_cxx_shared
            "${ANDROID_NDK}/toolchains/llvm/prebuilt/*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so")
        if(_eve_cxx_shared)
            list(GET _eve_cxx_shared 0 _eve_cxx_one)
            install(FILES "${_eve_cxx_one}" DESTINATION lib)
        endif()
    endif()
endif()

# ---- Selected third-party headers needed to compile plugins (SSQ bindings) ----
if(EVENGINE_THIRD_PARTY_BINARY_DIR)
    set(_eve_tp_inc "${EVENGINE_THIRD_PARTY_BINARY_DIR}")
else()
    set(_eve_tp_inc "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}")
endif()
if(NOT EVENGINE_THIRD_PARTY_BINARY_DIR AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_eve_tp_inc "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug")
endif()
get_filename_component(_eve_tp_inc "${_eve_tp_inc}" ABSOLUTE)
# The third-party tree is built by the deps target *after* configure, so the
# existence check must run at install time, not configure time (a configure-time
# guard silently drops these headers on fresh CI builds).
install(CODE "
    set(_eve_tp_inc \"${_eve_tp_inc}\")
    if(EXISTS \"\${_eve_tp_inc}/include\")
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/include\")
        foreach(_eve_hdr_dir IN ITEMS simplesquirrel squirrel SQLiteCpp)
            if(EXISTS \"\${_eve_tp_inc}/include/\${_eve_hdr_dir}\")
                file(COPY \"\${_eve_tp_inc}/include/\${_eve_hdr_dir}\"
                     DESTINATION \"\${CMAKE_INSTALL_PREFIX}/include\")
            endif()
        endforeach()
        # Flat squirrel headers sometimes live at include/*.h
        foreach(_eve_sq_hdr IN ITEMS squirrel.h sqstdio.h sqstdblob.h sqstdmath.h sqstdsystem.h sqstdstring.h sqconfig.h)
            if(EXISTS \"\${_eve_tp_inc}/include/\${_eve_sq_hdr}\")
                file(COPY \"\${_eve_tp_inc}/include/\${_eve_sq_hdr}\"
                     DESTINATION \"\${CMAKE_INSTALL_PREFIX}/include\")
            endif()
        endforeach()
    endif()
")

# ---- Marker files ----
install(CODE "
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/share/eve/VERSION\" \"${EVENGINE_SDK_VERSION}\\n\")
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/share/eve/TARGET_PLATFORM\" \"${BUILD_PLATFORM}\\n\")
")

# ---- CMake package config (find_package(EVEngine)) ----
set(EVENGINE_TARGET_PLATFORM "${BUILD_PLATFORM}")
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/EVEngineConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/EVEngineConfig.cmake"
    INSTALL_DESTINATION cmake
)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/EVEngineConfigVersion.cmake"
    VERSION "${EVENGINE_SDK_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_BINARY_DIR}/EVEngineConfig.cmake"
    "${CMAKE_BINARY_DIR}/EVEngineConfigVersion.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/EVEnginePlugin.cmake"
    DESTINATION cmake
)

install(FILES "${CMAKE_SOURCE_DIR}/cmake/sdk/README.md"
    DESTINATION share/eve
    OPTIONAL
)

# Desktop FreePats / TiMidity bank (win32 / linux / macosx only).
include(${CMAKE_SOURCE_DIR}/cmake/timidity_share.cmake)
eve_install_timidity_share()

