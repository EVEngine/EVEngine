# Opt-in Windows/MSVC object sharing. Include before project(), then call
# eve_finish_ccache_hardlink after compiler identification.
include_guard(GLOBAL)

macro(eve_prepare_ccache_hardlink)
    if(NOT CMAKE_HOST_WIN32 OR NOT CMAKE_GENERATOR MATCHES "Ninja")
        message(FATAL_ERROR "CCACHE_HARDLINK requires Windows and Ninja")
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.25)
        message(FATAL_ERROR "CCACHE_HARDLINK requires CMake 3.25 or newer")
    endif()
    find_program(EVENGINE_CCACHE_EXECUTABLE NAMES ccache REQUIRED)
    execute_process(COMMAND "${EVENGINE_CCACHE_EXECUTABLE}" --version
        OUTPUT_VARIABLE _eve_ccache_version COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCH "ccache version ([0-9.]+)" _eve_version_match "${_eve_ccache_version}")
    if(NOT _eve_version_match OR CMAKE_MATCH_1 VERSION_LESS 4.14.1)
        message(FATAL_ERROR "CCACHE_HARDLINK requires ccache 4.14.1 or newer")
    endif()
    # ccache must identify/execute cl.exe, not the repository's .cmd compiler
    # wrapper. The generated launcher below owns the console code page instead.
    foreach(_eve_lang C CXX)
        get_filename_component(_eve_compiler_name "${CMAKE_${_eve_lang}_COMPILER}" NAME)
        if(_eve_compiler_name STREQUAL "msvc-cl.cmd")
            find_program(_eve_native_cl NAMES cl REQUIRED)
            set(CMAKE_${_eve_lang}_COMPILER "${_eve_native_cl}" CACHE FILEPATH
                "Native MSVC compiler for ccache" FORCE)
        endif()
    endforeach()
    cmake_policy(SET CMP0141 NEW)
endmacro()

function(eve_finish_ccache_hardlink)
    if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "CCACHE_HARDLINK currently requires native MSVC")
    endif()
    set(EVENGINE_CCACHE_DIR "$ENV{LOCALAPPDATA}/EVEngine/ccache" CACHE PATH
        "Shared local ccache directory; must be on the build output volume")
    cmake_path(ABSOLUTE_PATH EVENGINE_CCACHE_DIR NORMALIZE OUTPUT_VARIABLE _eve_cache_dir)
    cmake_path(GET _eve_cache_dir ROOT_NAME _eve_cache_drive)
    cmake_path(GET CMAKE_BINARY_DIR ROOT_NAME _eve_build_drive)
    string(TOLOWER "${_eve_cache_drive}" _eve_cache_drive)
    string(TOLOWER "${_eve_build_drive}" _eve_build_drive)
    if(NOT _eve_cache_drive STREQUAL _eve_build_drive)
        message(FATAL_ERROR "CCACHE_HARDLINK cache and build directory must share a volume")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ccache")
    # Drive letters alone do not detect directory-mounted volumes or a file
    # system without hardlinks. Verify the operation on the actual directories.
    string(SHA256 _eve_probe_key "${CMAKE_BINARY_DIR}")
    file(MAKE_DIRECTORY "${_eve_cache_dir}")
    set(_eve_probe_source "${_eve_cache_dir}/evengine-hardlink-probe-${_eve_probe_key}.tmp")
    set(_eve_probe_target "${CMAKE_BINARY_DIR}/ccache/hardlink-probe.tmp")
    file(WRITE "${_eve_probe_source}" "EVEngine hardlink probe\n")
    file(CREATE_LINK "${_eve_probe_source}" "${_eve_probe_target}" RESULT _eve_probe_result)
    file(REMOVE "${_eve_probe_target}" "${_eve_probe_source}")
    if(NOT _eve_probe_result STREQUAL "0")
        message(FATAL_ERROR "CCACHE_HARDLINK cannot link cache objects into this build: ${_eve_probe_result}")
    endif()
    option(EVENGINE_CCACHE_TRIM_FILE_PATHS
        "Use empirically checked, undocumented MSVC /d1trimfile for root-relative __FILE__" OFF)
    set(_eve_extra_environment "")
    # ccache makes the source argument build-relative. /FC preserves absolute
    # __FILE__ semantics in the default mode; with /d1trimfile both source and
    # header expansions become source-root relative, not build-relative.
    add_compile_options(/FC)
    set(_eve_protocol "v2-fc")
    if(EVENGINE_CCACHE_TRIM_FILE_PATHS)
        # Never infer private-option support from a version number. Run a real
        # header expansion probe with the selected compiler before enabling it.
        file(WRITE "${CMAKE_BINARY_DIR}/ccache/trim-check.h"
            "#pragma once\ninline const char* file_path() { return __FILE__; }\n")
        file(WRITE "${CMAKE_BINARY_DIR}/ccache/trim-check.cpp"
            "#include \"trim-check.h\"\n#include <cstdio>\nint main() { std::printf(\"%s\", file_path()); }\n")
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/ccache/" _eve_probe_prefix)
        try_run(_eve_trim_run _eve_trim_compiles
            SOURCES "${CMAKE_BINARY_DIR}/ccache/trim-check.cpp"
            COMPILE_DEFINITIONS "/d1trimfile:${_eve_probe_prefix}"
            RUN_OUTPUT_VARIABLE _eve_trim_output
            COMPILE_OUTPUT_VARIABLE _eve_trim_diagnostics
            NO_CACHE)
        if(NOT _eve_trim_compiles OR NOT _eve_trim_run EQUAL 0 OR
           NOT _eve_trim_output STREQUAL "trim-check.h")
            message(FATAL_ERROR "MSVC /d1trimfile behavior probe failed: ${_eve_trim_output}\n${_eve_trim_diagnostics}")
        endif()
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/" _eve_trim_prefix)
        add_compile_options("/d1trimfile:${_eve_trim_prefix}")
        # Only this verified, root-specific spelling is ignored. Other options
        # and the complete preprocessed input remain part of the cache key.
        set(_eve_extra_environment "CCACHE_IGNOREOPTIONS=/d1trimfile:*")
        set(_eve_protocol "v3-trim-file-fc")
        message(STATUS "ccache: verified private MSVC /d1trimfile; __FILE__ becomes source-root relative")
    endif()
    get_property(_eve_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
    foreach(_eve_lang IN LISTS _eve_languages)
        if(NOT _eve_lang MATCHES "^(C|CXX)$")
            continue()
        endif()
        set(_eve_prefix "${CMAKE_${_eve_lang}_CL_SHOWINCLUDES_PREFIX}")
        string(STRIP "${_eve_prefix}" _eve_prefix)
        if(NOT _eve_prefix)
            message(FATAL_ERROR "Missing detected MSVC /showIncludes prefix for ${_eve_lang}")
        endif()
        # Namespace the corrected output protocol so old entries containing an
        # unrecognised localized dependency prefix cannot be replayed.
        string(SHA256 _eve_protocol_hash "${_eve_prefix}")
        set(_eve_command
            "\"${CMAKE_COMMAND}\" -E env \"CCACHE_DIR=${_eve_cache_dir}\" \"CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}\" CCACHE_HARDLINK=1 CCACHE_NOCOMPRESS=1 CCACHE_NODIRECT=1 CCACHE_NOHASHDIR=1 CCACHE_COMPILERTYPE=msvc \"CCACHE_MSVC_DEP_PREFIX=${_eve_prefix}\" ${_eve_extra_environment} CCACHE_NAMESPACE=EVEngine-msvc-hardlink-${_eve_protocol}-${_eve_protocol_hash} \"${EVENGINE_CCACHE_EXECUTABLE}\" %*")
        # Ninja hashes the launcher path, not .cmd contents. A setting/protocol
        # change must change the path so existing objects and dependency records
        # are refreshed instead of continuing to use old replayed paths.
        string(SHA256 _eve_launcher_hash "${_eve_command}")
        set(_eve_launcher "${CMAKE_BINARY_DIR}/ccache/launch-${_eve_lang}-${_eve_launcher_hash}.cmd")
        file(CONFIGURE OUTPUT "${_eve_launcher}"
            CONTENT "@echo off\nchcp 65001 >nul\n${_eve_command}\nexit /b %ERRORLEVEL%\n"
            @ONLY NEWLINE_STYLE CRLF)
        set(CMAKE_${_eve_lang}_COMPILER_LAUNCHER "${_eve_launcher}" PARENT_SCOPE)
    endforeach()
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>" PARENT_SCOPE)

    # Each root owns a separate immutable registry record. Publish atomically;
    # concurrent configurations never append to one shared registry file.
    # Schema v1 ignores unknown fields and rejects unknown schema/version pairs.
    set(_eve_registry "${_eve_cache_dir}/evengine-source-roots-v1")
    file(MAKE_DIRECTORY "${_eve_registry}")
    string(SHA256 _eve_root_key "${CMAKE_SOURCE_DIR}")
    string(SHA256 _eve_writer_key "${CMAKE_BINARY_DIR}")
    set(_eve_record "{\"schema\":\"evengine.ccache-source-root\",\"version\":1,\"source_root\":\"${CMAKE_SOURCE_DIR}\"}\n")
    set(_eve_temporary "${_eve_registry}/${_eve_root_key}-${_eve_writer_key}.tmp")
    file(WRITE "${_eve_temporary}" "${_eve_record}")
    file(RENAME "${_eve_temporary}" "${_eve_registry}/${_eve_root_key}.json")
    set(_eve_map_generator "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ccache_source_map.cmake")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DREGISTRY=${_eve_registry}"
        "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT=${CMAKE_BINARY_DIR}/ccache/sourceFileMap.json"
        -P "${_eve_map_generator}" COMMAND_ERROR_IS_FATAL ANY)
    add_custom_target(eve_ccache_source_map ALL
        COMMAND "${CMAKE_COMMAND}" "-DREGISTRY=${_eve_registry}"
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            "-DOUTPUT=${CMAKE_BINARY_DIR}/ccache/sourceFileMap.json"
            -P "${_eve_map_generator}"
        COMMENT "Refreshing ccache debugger source mappings"
        VERBATIM)
    message(STATUS "ccache hardlinks: ${_eve_cache_dir}; detected localized MSVC dependency prefixes")
    message(STATUS "Shared /Z7 debugging requires sourceFileMap: ${CMAKE_BINARY_DIR}/ccache/sourceFileMap.json")
endfunction()

# Target-specific Makefile invocations must refresh mappings too, even when
# they bypass ALL. Attach only to compiled targets, never the map target itself.
function(eve_attach_ccache_source_map directory)
    get_property(_eve_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_eve_target IN LISTS _eve_targets)
        get_target_property(_eve_type "${_eve_target}" TYPE)
        if(_eve_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            add_dependencies("${_eve_target}" eve_ccache_source_map)
        endif()
    endforeach()
    get_property(_eve_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_eve_child IN LISTS _eve_children)
        eve_attach_ccache_source_map("${_eve_child}")
    endforeach()
endfunction()
