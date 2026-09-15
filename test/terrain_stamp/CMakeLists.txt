cmake_minimum_required(VERSION 3.21)
project(EVEngineTerrainStampTests LANGUAGES C CXX)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(USE_MOLD OFF CACHE BOOL "" FORCE)
add_subdirectory("${EVE_ROOT}/external/zeroerr" zeroerr)
target_compile_definitions(zeroerr PUBLIC ZEROERR_NO_MAIN)

# Real CPU and binding sources, deliberately without graphics/editor providers.
add_library(terrain_stamp_core STATIC
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainStamp.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainDetailLayer.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainDetailPlacement.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainTreePlacement.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainMultiTile.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainGenerationSession.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainMask.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainImageMask.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainEffect.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainErosion.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainSediment.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainWaterField.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/Heightmap.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainSampler.cpp"
    "${EVE_ROOT}/src/modules/procgen/texture/NoiseField.cpp"
    "${EVE_ROOT}/src/modules/procgen/Grid2D.cpp"
    "${EVE_ROOT}/src/modules/procgen/PointSet.cpp"
    "${EVE_ROOT}/src/modules/procgen/AttributeTable.cpp"
    "${EVE_ROOT}/src/modules/procgen/Params.cpp"
    "${EVE_ROOT}/src/modules/procgen/Semantic.cpp"
    "${EVE_ROOT}/src/engine/common/Value.cpp"
    "${EVE_ROOT}/src/engine/common/Json.cpp")
target_include_directories(terrain_stamp_core PUBLIC "${EVE_ROOT}/src/engine" "${EVE_ROOT}/src/modules")
target_link_libraries(terrain_stamp_core PUBLIC zeroerr)

file(GLOB squirrel_sources "${EVE_ROOT}/third-party/squirrel/squirrel/*.cpp"
    "${EVE_ROOT}/third-party/squirrel/sqstdlib/*.cpp"
    "${EVE_ROOT}/third-party/simplesquirrel/source/*.cpp")
add_library(terrain_stamp_script STATIC ${squirrel_sources}
    "${EVE_ROOT}/src/engine/common/SquirrelBinding.cpp"
    "${EVE_ROOT}/src/modules/procgen/heightmap/TerrainStampScript.cpp")
target_include_directories(terrain_stamp_script PUBLIC "${EVE_ROOT}/third-party/squirrel/include"
    "${EVE_ROOT}/third-party/simplesquirrel/include")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(terrain_stamp_script PUBLIC _SQ64)
endif()
target_link_libraries(terrain_stamp_script PUBLIC terrain_stamp_core)
add_executable(terrain_stamp_tests "${EVE_ROOT}/test/profile/gltf_animation_main.cpp"
    "${EVE_ROOT}/test/terrain_stamp.cpp" "${EVE_ROOT}/test/terrain_stamp_script.cpp"
    "${EVE_ROOT}/test/terrain_mask.cpp" "${EVE_ROOT}/test/terrain_effect.cpp" "${EVE_ROOT}/test/terrain_erosion.cpp" "${EVE_ROOT}/test/terrain_water.cpp" "${EVE_ROOT}/test/terrain_sediment.cpp" "${EVE_ROOT}/test/terrain_generation_session.cpp" "${EVE_ROOT}/test/terrain_detail.cpp" "${EVE_ROOT}/test/terrain_tree.cpp" "${EVE_ROOT}/test/terrain_multitile.cpp" "${EVE_ROOT}/test/grass_billboard.cpp"
    "${EVE_ROOT}/src/modules/graphics/GrassBillboards.cpp"
    "${EVE_ROOT}/src/modules/graphics/VegetationWind.cpp"
    "${EVE_ROOT}/src/modules/graphics/GrassWind.cpp"
    "${EVE_ROOT}/src/modules/graphics/TreeWind.cpp"
    "${EVE_ROOT}/src/modules/graphics/GrassWindBindings.cpp"
    "${EVE_ROOT}/src/modules/graphics/GrassParameters.cpp"
    "${EVE_ROOT}/src/modules/graphics/Shader.cpp"
    "${EVE_ROOT}/src/engine/common/Exception.cpp"
    "${EVE_ROOT}/src/engine/common/RenderTrace.cpp"
    "${EVE_ROOT}/test/grass_wind.cpp"
    "${EVE_ROOT}/test/tree_wind.cpp")
target_link_libraries(terrain_stamp_tests PRIVATE terrain_stamp_script)
target_include_directories(terrain_stamp_tests PRIVATE "${EVE_ROOT}/third-party/glm")
if(MSVC)
    foreach(target terrain_stamp_core terrain_stamp_script terrain_stamp_tests)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /bigobj)
    endforeach()
endif()
enable_testing()
foreach(source terrain_stamp.cpp terrain_stamp_script.cpp terrain_mask.cpp terrain_effect.cpp terrain_erosion.cpp terrain_water.cpp terrain_sediment.cpp terrain_generation_session.cpp terrain_detail.cpp terrain_tree.cpp terrain_multitile.cpp grass_billboard.cpp grass_wind.cpp tree_wind.cpp)
    file(STRINGS "${EVE_ROOT}/test/${source}" cases REGEX "^TEST_CASE\\(\"")
    foreach(line IN LISTS cases)
        string(REGEX REPLACE "TEST_CASE\\(\"([^\"]+)\".*" "\\1" name "${line}")
        string(REPLACE "." "\\." pattern "${name}")
        add_test(NAME "${name}" COMMAND terrain_stamp_tests "--testcase=^${pattern}$")
        # A stale executable with no matching case otherwise exits successfully in zeroerr.
        set_tests_properties("${name}" PROPERTIES
            PASS_REGULAR_EXPRESSION "TEST CASE:[ \t]+1[ \t]+0[ \t]+0[ \t]+0"
            FAIL_REGULAR_EXPRESSION "Assertion Failed" LABELS "procgen;cpu;stamp")
    endforeach()
endforeach()
