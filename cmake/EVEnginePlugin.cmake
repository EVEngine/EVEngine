# Helpers for building native plugins against a target-platform EVEngine SDK.
# Plugins are SHARED libraries loaded by eve.plugins.load; they do not link libeve.

function(add_eve_plugin _eve_plugin_name)
    cmake_parse_arguments(_EVE_P "" "" "SOURCES;INCLUDE_DIRS;LIBRARIES" ${ARGN})
    if(NOT _EVE_P_SOURCES)
        message(FATAL_ERROR "add_eve_plugin(${_eve_plugin_name}): SOURCES required")
    endif()
    if(NOT EVENGINE_ROOT)
        message(FATAL_ERROR "add_eve_plugin: EVENGINE_ROOT unset (call find_package(EVEngine) first)")
    endif()

    add_library(${_eve_plugin_name} SHARED ${_EVE_P_SOURCES})
    target_compile_features(${_eve_plugin_name} PUBLIC cxx_std_20)
    target_compile_definitions(${_eve_plugin_name} PRIVATE EVENGINE_PLUGIN=1)
    target_include_directories(${_eve_plugin_name} PRIVATE
        "${EVENGINE_INCLUDE_DIR}"
        "${EVENGINE_THIRD_PARTY_INCLUDE_DIR}"
        ${_EVE_P_INCLUDE_DIRS}
    )

    if(WIN32)
        # Link against the import library generated from eve.exe exports.
        if(EXISTS "${EVENGINE_LIB_DIR}/eve.lib")
            target_link_libraries(${_eve_plugin_name} PRIVATE "${EVENGINE_LIB_DIR}/eve.lib")
        elseif(EXISTS "${EVENGINE_LIB_DIR}/eve.dll.a")
            target_link_libraries(${_eve_plugin_name} PRIVATE "${EVENGINE_LIB_DIR}/eve.dll.a")
        else()
            message(WARNING "add_eve_plugin: eve import library not found under ${EVENGINE_LIB_DIR}")
        endif()
    elseif(APPLE)
        # Resolve ModuleManager etc. from the host at load time.
        target_link_options(${_eve_plugin_name} PRIVATE
            "LINKER:-undefined,dynamic_lookup"
        )
    endif()
    # Linux / Android: leave host symbols undefined; resolved when eve dlopens the plugin.

    if(_EVE_P_LIBRARIES)
        target_link_libraries(${_eve_plugin_name} PRIVATE ${_EVE_P_LIBRARIES})
    endif()

    set_target_properties(${_eve_plugin_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${_eve_plugin_name}"
    )
endfunction()
