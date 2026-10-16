# Registration-only modules have no referenced import that forces their DLL to
# load. Derive host imports from the same resolved link-group manifest; this is
# not a second module boot list and does not instantiate optional providers.
if(NOT EVENGINE_NATIVE_TARGET OR NOT TARGET "${EVENGINE_NATIVE_TARGET}")
    return()
endif()

set(_eve_imports "// Generated from enabled link groups; do not edit.\n")
string(APPEND _eve_imports "#if defined(_WIN32)\n#define EVE_GROUP_IMPORT __declspec(dllimport)\n#else\n#define EVE_GROUP_IMPORT\n#endif\n")
set(_eve_import_calls "")
foreach(_eve_import_group IN LISTS EVE_LINK_GROUP_NAMES)
    if(TARGET "${_eve_import_group}")
        string(APPEND _eve_imports
            "extern \"C\" EVE_GROUP_IMPORT void eve_link_group_${_eve_import_group}();\n")
        string(APPEND _eve_import_calls "eve_link_group_${_eve_import_group}();\n")
    endif()
endforeach()
string(APPEND _eve_imports
    "namespace { struct LoadEnabledGroups { LoadEnabledGroups() {\n${_eve_import_calls}} }; const LoadEnabledGroups loadEnabledGroups; }\n")
set(_eve_import_file "${CMAKE_BINARY_DIR}/generated/LinkGroupImports.cpp")
file(GENERATE OUTPUT "${_eve_import_file}" CONTENT "${_eve_imports}")
target_sources("${EVENGINE_NATIVE_TARGET}" PRIVATE "${_eve_import_file}")
