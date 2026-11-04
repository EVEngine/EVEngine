# Derive providers from the actual target, never a second manual boot list.
get_filename_component(_mcp_repository "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_target_property(_mcp_sources EVDevTools SOURCES)
list(FILTER _mcp_sources INCLUDE REGEX "\\.cpp$")
list(SORT _mcp_sources)
set(_mcp_inventory "${CMAKE_CURRENT_BINARY_DIR}/mcp-family-sources.txt")
string(JOIN "\n" _mcp_inventory_text ${_mcp_sources})
file(CONFIGURE OUTPUT "${_mcp_inventory}" CONTENT "${_mcp_inventory_text}\n" @ONLY)
set(_mcp_generated "${CMAKE_CURRENT_BINARY_DIR}/devtools/McpToolFamilies.generated.cpp")
add_custom_command(
    OUTPUT "${_mcp_generated}"
    COMMAND "${Python3_EXECUTABLE}" "${_mcp_repository}/scripts/generate_mcp_families.py"
            --sources "${_mcp_inventory}" --output "${_mcp_generated}"
    DEPENDS "${_mcp_inventory}" ${_mcp_sources}
            "${_mcp_repository}/scripts/generate_mcp_families.py"
            "${_mcp_repository}/scripts/cpp_source.py"
    COMMENT "Composing compiled MCP tool families"
    VERBATIM)
target_sources(EVDevTools PRIVATE "${_mcp_generated}")
# Newly added provider translation units are picked up by a normal build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}")
