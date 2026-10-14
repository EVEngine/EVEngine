# Platform resolution intentionally exports variables into the caller.
include_guard(GLOBAL)

macro(get_available_build)
    set(AVAILABLE_PLATFORM "")

    # BUILD_PLATFORM predates FetchContent usage and is also owned by Dawn.
    # Prefer the namespaced override when supplied so dependency configuration
    # and automatic top-level reconfiguration cannot change EVEngine's target.
    if(EVENGINE_BUILD_PLATFORM)
        set(BUILD_PLATFORM "${EVENGINE_BUILD_PLATFORM}")
        set(BUILD_PLATFORM "${EVENGINE_BUILD_PLATFORM}" CACHE STRING
            "The EVEngine target build platform" FORCE)
    endif()

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
        set(_eve_excluded_backend_subdirs "vulkan")
        set(_eve_backend_exclusion_help "Backend subdirs excluded from webgpu builds")
    else()
        set(_eve_excluded_backend_subdirs "webgpu")
        set(_eve_backend_exclusion_help "Backend subdirs excluded from native builds")
    endif()
    # Keep both bindings synchronized. With CMP0126=NEW, updating a CACHE entry
    # does not replace a same-named normal variable in the current scope, and
    # create_module() reads the normal variable first.
    set(EVENGINE_EXCLUDED_BACKEND_SUBDIRS "${_eve_excluded_backend_subdirs}")
    set(EVENGINE_EXCLUDED_BACKEND_SUBDIRS "${_eve_excluded_backend_subdirs}"
        CACHE INTERNAL "${_eve_backend_exclusion_help}" FORCE)

    # Poco-heavy sources trimmed from the WASM (Emscripten) build. The data
    # module keeps ByteData (needed by gpgpu/webgpu) but drops the JSON/XML/
    # DataModule files that require Poco. Font/FreeType is supported on WASM
    # via the Emscripten port. Snow.cpp follows weather's OPTIONAL_DEPS procgen
    # (see src/modules/CMakeLists.txt) rather than an Emscripten-only list.
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(_eve_excluded_module_files
            "DataModule.cpp;JsonDocument.cpp;XmlDocument.cpp;ImGuiHostPanels.cpp")
        set(_eve_module_exclusion_help
            "Exact basenames excluded from module source scan on Emscripten")
    else()
        set(_eve_excluded_module_files "ImGuiHostPanels.cpp")
        set(_eve_module_exclusion_help "Exact basenames excluded from module source scan")
    endif()
    set(EVENGINE_EXCLUDED_MODULE_FILES "${_eve_excluded_module_files}")
    set(EVENGINE_EXCLUDED_MODULE_FILES "${_eve_excluded_module_files}"
        CACHE INTERNAL "${_eve_module_exclusion_help}" FORCE)

    message(STATUS "Backend source exclusions: ${EVENGINE_EXCLUDED_BACKEND_SUBDIRS}")

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
