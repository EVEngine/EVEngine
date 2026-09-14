cmake_minimum_required(VERSION 3.21)
project(EVEngineTilemapEditorTests LANGUAGES C CXX)

# Compile the production CPU editing code without an engine host or GPU.
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EVE_ZEROERR_SOURCE "${EVE_ROOT}/external/zeroerr" CACHE PATH "Pinned zeroerr checkout")
set(EVE_ECS_SOURCE "${EVE_ROOT}/external/ECS.hpp" CACHE PATH "Pinned ECS checkout")
option(EVE_TILEMAP_RUNTIME_TESTS "Include live ECS map adapter tests" ON)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ZEROERR_SOURCE}" zeroerr)

set(case_sources "${EVE_ROOT}/test/editor_level.cpp" "${EVE_ROOT}/test/editor_tilemap_formats.cpp")
add_executable(tilemap_editor_tests
    ${case_sources}
    "${EVE_ROOT}/src/engine/common/Json.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp"
    "${EVE_ROOT}/src/modules/map/level/editing/LevelDocument.cpp"
    "${EVE_ROOT}/src/modules/map/level/editing/LevelFormat.cpp"
    "${EVE_ROOT}/src/modules/map/level/editing/Brush.cpp"
    "${EVE_ROOT}/src/modules/map/level/editing/EditorHistory.cpp"
    "${EVE_ROOT}/src/modules/map/level/editing/TileBuffer.cpp")
target_include_directories(tilemap_editor_tests PRIVATE "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(tilemap_editor_tests PRIVATE zeroerr)

if(EVE_TILEMAP_RUNTIME_TESTS)
    find_path(EVE_GLM_INCLUDE glm/vec4.hpp HINTS "${EVE_ROOT}/third-party/glm" REQUIRED)
    target_include_directories(tilemap_editor_tests PRIVATE "${EVE_ECS_SOURCE}/src" "${EVE_GLM_INCLUDE}")
    target_sources(tilemap_editor_tests PRIVATE
        "${EVE_ROOT}/src/modules/map/TileLayer.cpp"
        "${EVE_ROOT}/src/modules/map/editing/TileLayerTarget.cpp"
        "${EVE_ROOT}/test/map_tile_editing.cpp")
    list(APPEND case_sources "${EVE_ROOT}/test/map_tile_editing.cpp")
endif()

# Discard unused GPU/config entry points; the exercised production functions
# are compiled unchanged and no backend symbols are stubbed.
if(MSVC)
    target_compile_options(tilemap_editor_tests PRIVATE /utf-8 /EHsc /Gy /W4)
    target_link_options(tilemap_editor_tests PRIVATE /OPT:REF)
else()
    target_compile_options(tilemap_editor_tests PRIVATE -Wall -Wextra -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(tilemap_editor_tests PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(tilemap_editor_tests PRIVATE -Wl,--gc-sections)
    endif()
endif()

enable_testing()
foreach(source IN LISTS case_sources)
    file(STRINGS "${source}" cases REGEX "^TEST_CASE\\(\"")
    foreach(line IN LISTS cases)
        string(REGEX REPLACE "^TEST_CASE\\(\"([^\"]+)\".*" "\\1" name "${line}")
        string(REPLACE "." "\\." pattern "${name}")
        add_test(NAME "${name}" COMMAND tilemap_editor_tests "--testcase=^${pattern}$")
        set_tests_properties("${name}" PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "tilemap;cpu")
    endforeach()
endforeach()
