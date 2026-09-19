# One shared library per link group (see EVE_LINK_GROUP_TABLE in
# cmake/module_manifest.cmake).
#
# Modules stay OBJECT libraries: the module manifest, the profiles, per-module
# compile settings and the source scan are all untouched. A group DLL aggregates
# the objects of its modules, and that aggregation is the entire point -- the
# engine's debug information then lives in one PDB per group instead of being
# merged into every consumer executable. A per-domain test PDB measured 1.2 GB
# for a 0.18 GB executable, and only ~0.8 MB per test file of that was the test
# code.
#
# A single whole-engine DLL is not an option: it needs
# WINDOWS_EXPORT_ALL_SYMBOLS, which the engine already exceeds (MSVC LNK1189,
# 65535 exports; see src/engine/CMakeLists.txt). One group is a fraction of that
# surface, and the build reports LNK1189 if a group is still too large.
#
# Included after add_subdirectory(src/modules) so every module target exists.
# Consumers only ever see EVE_LINK_TARGETS (set by eve_resolve_modules) and never
# need to know how modules are grouped.

if(NOT EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
    return()
endif()

# The host, the tests, the benchmarks and the plugins all consume the annotated
# engine surface from these DLLs, so their view of EVENGINE_API is dllimport.
# Modules get this too (eve_engine_includes is what they link), but Export.h
# checks EVENGINE_ENGINE_EXPORTS first, so the defining side still exports.
target_compile_definitions(eve_engine_includes INTERFACE EVENGINE_MODULE_DLL)

# The same external closure the host and the test runner link. ThirdParty is a
# directory-scoped variable in src/engine and test/, so this file derives its own
# copy from the manifest rather than reading theirs.
eve_append_system_libraries(_eve_group_system_libs)
eve_thirdparty_libs(_eve_group_tp_libs ${EVE_THIRDPARTY_GROUPS})

foreach(_eve_group IN LISTS EVE_LINK_GROUP_NAMES)
    set(_eve_group_objects "")
    set(_eve_group_modules "")
    foreach(m IN LISTS EVE_ENABLED_MODULES)
        if(EVE_MODULE_${m}_LINK_GROUP STREQUAL _eve_group)
            list(APPEND _eve_group_objects "$<TARGET_OBJECTS:${EVE_MODULE_${m}_LIB}>")
            list(APPEND _eve_group_modules "${m}")
        endif()
    endforeach()

    add_library(${_eve_group} SHARED ${_eve_group_objects})
    target_link_libraries(${_eve_group} PRIVATE eve_engine_includes)
    if(TARGET eve_imgui)
        target_link_libraries(${_eve_group} PRIVATE eve_imgui)
    endif()
    # Group-to-group edges. EVE_LINK_GROUP_NAMES is in table order (lowest layer
    # first) and the manifest graph has only two edges pointing at a higher layer
    # (cmdline/devtools into filesystem/platform_event), which merging layers -1
    # and 0 absorbs. Linking every earlier group therefore yields the complete
    # downward closure with no import cycle.
    target_link_libraries(${_eve_group} PRIVATE ${_eve_lower_groups})
    target_link_libraries(${_eve_group} PRIVATE
        EVScripts zeroerr ${_eve_group_tp_libs} ${_eve_group_system_libs}
        ${EVENGINE_VULKAN_LIB} ${EVENGINE_WEBGPU_LIB})
    if(MSVC)
        # MSVC pulls the CRT from /DEFAULTLIB directives carried by the objects.
        # A shared library whose sources are all $<TARGET_OBJECTS:...> does not
        # reliably end up with the debug UCRT import library, and the static
        # debug third-party archives reference the import thunks directly
        # (__imp__calloc_dbg from Poco), so name it explicitly.
        target_link_libraries(${_eve_group} PRIVATE
            $<$<CONFIG:Debug>:ucrtd>
            $<$<NOT:$<CONFIG:Debug>>:ucrt>)
    endif()
    if(NOT EVENGINE_PROFILE_HOSTLESS)
        add_dependencies(${_eve_group} third-party)
    endif()
    list(APPEND _eve_lower_groups "${_eve_group}")

    # The export surface is the EVENGINE_API-annotated API only (see
    # src/engine/common/Export.h). WINDOWS_EXPORT_ALL_SYMBOLS is deliberately NOT
    # enabled: it exports every symbol the objects define -- 75,885 for the
    # 39-module EVFoundation group, measured from its generated .exports.def --
    # which trips MSVC LNK1189's 65535 limit. The module objects already compile
    # exactly the annotated declarations to dllexport.
    if(WIN32)
        set_target_properties(${_eve_group} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    else()
        # ELF/Mach-O: the annotated symbols carry default visibility themselves
        # (Export.h); inline code stays hidden so it is emitted in the consumer
        # instead of being duplicated into every group.
        set_target_properties(${_eve_group} PROPERTIES
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN OFF
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()

    list(LENGTH _eve_group_modules _eve_group_module_count)
    message(STATUS "Link group ${_eve_group}: ${_eve_group_module_count} modules -> one shared library")
endforeach()
