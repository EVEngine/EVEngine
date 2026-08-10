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
else()
    install(TARGETS ${EVENGINE_NATIVE_TARGET}
        RUNTIME DESTINATION bin
        # Windows import library for plugins linking against eve.exe exports
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
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

set(_eve_module_dirs
    animation audio data event filesystem graphics image ik joystick keyboard mouse
    model3d network particles plugins sound spatial timer touch ui window
)
foreach(_eve_mod IN LISTS _eve_module_dirs)
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/modules/${_eve_mod}")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/src/modules/${_eve_mod}/"
            DESTINATION include/eve/${_eve_mod}
            FILES_MATCHING
                PATTERN "*.h"
                PATTERN "*.hpp"
                PATTERN "sdl" EXCLUDE
                PATTERN "vulkan" EXCLUDE
                PATTERN "physfs" EXCLUDE
                PATTERN "openal" EXCLUDE
                PATTERN "imgui" EXCLUDE
                PATTERN "cppfs" EXCLUDE
                PATTERN "include_shim" EXCLUDE
        )
    endif()
endforeach()

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
    set(_eve_tp_lib "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
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
set(_eve_tp_inc "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_eve_tp_inc "${CMAKE_SOURCE_DIR}/build/third-party-binary/${BUILD_PLATFORM}-debug")
endif()
if(EXISTS "${_eve_tp_inc}/include")
    foreach(_eve_hdr_dir IN ITEMS simplesquirrel squirrel SQLiteCpp)
        if(EXISTS "${_eve_tp_inc}/include/${_eve_hdr_dir}")
            install(DIRECTORY "${_eve_tp_inc}/include/${_eve_hdr_dir}"
                DESTINATION include
            )
        endif()
    endforeach()
    # Flat squirrel headers sometimes live at include/*.h
    file(GLOB _eve_sq_headers
        "${_eve_tp_inc}/include/squirrel.h"
        "${_eve_tp_inc}/include/sqstdio.h"
        "${_eve_tp_inc}/include/sqstdblob.h"
        "${_eve_tp_inc}/include/sqstdmath.h"
        "${_eve_tp_inc}/include/sqstdsystem.h"
        "${_eve_tp_inc}/include/sqstdstring.h"
        "${_eve_tp_inc}/include/sqconfig.h"
    )
    if(_eve_sq_headers)
        install(FILES ${_eve_sq_headers} DESTINATION include)
    endif()
endif()

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

