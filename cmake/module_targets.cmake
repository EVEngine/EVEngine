# Build helpers; included by the root before main().
include_guard(GLOBAL)

# configure_file already preserves the timestamp when content is unchanged.
# Keep this entry point for existing callers and support CMake's absolute paths.
function(eve_configure_file_if_changed input output)
    configure_file("${input}" "${output}")
endfunction()

# Create a manifest module and its stable configure-time source list.
# Temporary scan/export variables stay local to this function.

function(create_module libname folder)
    # Join the exclude lists with ',' instead of ';': scan_source.cmake is
    # run through a shell (Ninja on Linux/macOS), where both ';' and '|' are
    # shell metacharacters (command separator / pipe) that would split or
    # execute the -D argument. ',' is safe unquoted on every shell (POSIX sh,
    # cmd.exe, emcc). The scan script splits on ','.
    eve_module_scan_dir("${folder}" _eve_scan_dir)
    eve_module_nested_scan_excludes("${folder}" _eve_nested_dirs)
    set(_eve_excl_dirs ${EVENGINE_EXCLUDED_BACKEND_SUBDIRS} ${_eve_nested_dirs})
    string(JOIN "," _eve_excl_dirs_arg ${_eve_excl_dirs})
    string(JOIN "," _eve_excl_files_arg ${EVENGINE_EXCLUDED_MODULE_FILES})
    # Scan .cpp files at configure time. New files are picked up by the next
    # cmake configure (`make debug` reconfigures when the module directory is
    # newer than this list). Do not attach ALL rescan targets or
    # CMAKE_CONFIGURE_DEPENDS to the list file: rewriting it at build time
    # restacks Ninja cmake re-runs and rebuilds every TU.
    execute_process(COMMAND ${CMAKE_COMMAND}
        -Doutput_dir=${CMAKE_CURRENT_BINARY_DIR}
        -Dscan_dir=${_eve_scan_dir}
        -Dlist_name=${folder}
        -Dexclude_dirs=${_eve_excl_dirs_arg}
        -Dexclude_files=${_eve_excl_files_arg}
        -P ${CMAKE_SOURCE_DIR}/cmake/scan_source.cmake
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _eve_scan_result)
    if(NOT _eve_scan_result STREQUAL "0")
        message(FATAL_ERROR "Source scan failed for module '${folder}' (${libname}): ${_eve_scan_result}")
    endif()

    file(READ ${CMAKE_CURRENT_BINARY_DIR}/${folder}_src.txt module_src)
    string(REPLACE "\n" ";" module_src "${module_src}")
    # Drop empty entries from trailing newlines. A module whose only TUs were
    # optional-bridge exclusions must not call add_library with no SOURCES.
    list(FILTER module_src EXCLUDE REGEX "^$")
    if(NOT module_src)
        message(FATAL_ERROR
            "Module '${folder}' (${libname}) has no sources after "
            "EVENGINE_EXCLUDED_MODULE_FILES filtering. Either keep a stub TU, "
            "remove the module from this profile, or make its optional dep a "
            "hard DEPS so the module is disabled when the bridge is absent.")
    endif()

    # Xcode + OBJECT libraries often emit hashed object filenames that the
    # dependent link phase still references without the hash (duplicate basenames
    # like Window.cpp). Use STATIC archives under Xcode to keep linking stable.
    if(CMAKE_GENERATOR MATCHES "Xcode")
        add_library(${libname} STATIC ${module_src})
    else()
        add_library(${libname} OBJECT ${module_src})
    endif()
    # In SHARED mode these objects are aggregated into a link-group shared
    # library (cmake/link_groups.cmake), so they must be position independent on
    # ELF/Mach-O. The module stays an OBJECT library: the manifest, profiles,
    # per-module compile settings and the source scan are unchanged, and the
    # grouping lives entirely in EVE_LINK_GROUP_TABLE.
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
        set_target_properties(${libname} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # These objects are the defining side of the module DLL boundary, so the
        # EVENGINE_API-annotated declarations compile to dllexport (see
        # src/engine/common/Export.h). Checked before EVENGINE_MODULE_DLL, which
        # eve_engine_includes hands to every target including these.
        target_compile_definitions(${libname} PRIVATE EVENGINE_ENGINE_EXPORTS)
        # ...but only for THIS module's own link group. The group DLL exports
        # what its own modules declare and imports everything the lower groups
        # declare -- re-exporting a lower group's declaration would reference
        # the plain symbol instead of __imp_<symbol>, which nothing defines for
        # data (see the EVENGINE_API_<GROUP> block in
        # src/engine/common/Export.h). Exactly one define, never the other six.
        # EVE_MODULE_<name>_LINK_GROUP is resolved by eve_resolve_modules()
        # (cmake/modules.cmake), which main() runs before src/engine and
        # src/modules are added.
        set(_eve_module_group "${EVE_MODULE_${folder}_LINK_GROUP}")
        if(NOT _eve_module_group)
            message(FATAL_ERROR
                "Module '${folder}' has no EVE_MODULE_${folder}_LINK_GROUP; "
                "eve_resolve_modules() must run before create_module()")
        endif()
        string(REGEX REPLACE "^EV" "" _eve_module_group "${_eve_module_group}")
        string(TOUPPER "${_eve_module_group}" _eve_module_group)
        target_compile_definitions(${libname} PRIVATE
            "EVENGINE_EXPORTS_${_eve_module_group}")
        # The vulkan-hpp default dispatcher is a data symbol defined by exactly
        # one TU (src/modules/graphics/vulkan/Graphics.cpp, via VKB_IMPL) and
        # referenced by every group that calls vulkan-hpp enhanced-mode
        # overloads. Its owning group declares it dllexport here while every
        # other consumer gets dllimport from VULKAN_HPP_STORAGE_SHARED
        # (cmake/link_groups.cmake): vulkan-hpp offers exactly this pair of
        # hooks, and data symbols cannot cross a DLL boundary without it.
        if(_eve_module_group STREQUAL "BACKENDS")
            target_compile_definitions(${libname} PRIVATE VULKAN_HPP_STORAGE_SHARED_EXPORT)
        endif()
    endif()
    # Opt the module into the shared engine include set (was global
    # include_directories before; see eve_engine_includes in main()).
    target_link_libraries(${libname} PUBLIC eve_engine_includes)

    if(NOT EVENGINE_PROFILE_HOSTLESS)
        add_dependencies(${libname} third-party)
    endif()
endfunction()
