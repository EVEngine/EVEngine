cmake_minimum_required(VERSION 3.21)
project(EVRoadParity LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(EVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EVE_DEPENDENCY_TREE "" CACHE PATH "Existing EVEngine checkout with initialized zeroerr and its Debug build")
if(NOT EXISTS "${EVE_DEPENDENCY_TREE}/external/zeroerr/include/zeroerr/unittest.h")
    message(FATAL_ERROR "Set EVE_DEPENDENCY_TREE to an initialized EVEngine checkout")
endif()
set(_zero "${EVE_DEPENDENCY_TREE}/external/zeroerr")
set(_zero_build "${EVE_DEPENDENCY_TREE}/build/win32-debug/external/zeroerr")
set(_road_sources
    ${EVE_ROOT}/src/engine/common/Identity.cpp
    ${EVE_ROOT}/src/engine/common/Uri.cpp
    ${EVE_ROOT}/src/engine/common/ResourceRef.cpp
    ${EVE_ROOT}/src/engine/common/Capability.cpp
    ${EVE_ROOT}/src/engine/common/CapabilityOwned.cpp
    ${EVE_ROOT}/src/engine/common/Value.cpp
    ${EVE_ROOT}/src/engine/common/Json.cpp
    ${EVE_ROOT}/src/modules/procgen/MeshBuild.cpp
    ${EVE_ROOT}/src/modules/procgen/Grid2D.cpp
    ${EVE_ROOT}/src/modules/procgen/PointSet.cpp
    ${EVE_ROOT}/src/modules/procgen/PointSetAttributeOps.cpp
    ${EVE_ROOT}/src/modules/procgen/AttributeTable.cpp
    ${EVE_ROOT}/src/modules/procgen/spline/SplineCircularArc.cpp
    ${EVE_ROOT}/src/modules/procgen/spline/SplinePath.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadNetworkCodec.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadRecipeBake.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadRecipeNetwork.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadNetwork.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadNetworkRecipes.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadConnection.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadNetworkSplit.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadAuthoring.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadFacilityPrefab.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeProfiles.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeFacilityLinks.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeStructures.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadTerrain.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadTerrainPlan.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadGeometrySources.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadTerrainWorkspace.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadTerrainWorkspaceCodec.cpp
    ${EVE_ROOT}/src/modules/procgen/heightmap/Heightmap.cpp
    ${EVE_ROOT}/src/modules/procgen/heightmap/TerrainSampler.cpp
    ${EVE_ROOT}/src/modules/procgen/texture/NoiseField.cpp
    ${EVE_ROOT}/src/modules/procgen/Params.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadProfile.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBake.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeEdges.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadSampling.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadSurfacePath.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadParameterSampling.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeJunctions.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakeNavigation.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadBakePlacements.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadPlacementTerrain.cpp
    ${EVE_ROOT}/src/modules/procgen/road/RoadTraffic.cpp
    ${EVE_ROOT}/src/modules/property_access/PropertyAccess.cpp
    ${EVE_ROOT}/src/modules/editing/EditingProperty.cpp
    ${EVE_ROOT}/src/modules/editing/EditingValue.cpp
    ${EVE_ROOT}/src/modules/editing/EditingAuthority.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadPresetDocument.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkEditTarget.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkConnectionCodec.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkStyleCodec.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkPointCodec.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkEditApply.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkEditOperations.cpp
    ${EVE_ROOT}/src/modules/procgen/editing/RoadNetworkEditingCommands.cpp)
set(ROAD_PARITY_LINKAGE "STATIC" CACHE STRING "STATIC or SHARED for independent CPU road linkage validation")
foreach(_terrain_source
    TerrainStamp TerrainMeshStamp TerrainDetailLayer TerrainMultiTile TerrainSplatmap
    TerrainWorldWorkspace TerrainWorldRoad TerrainTreePlacement TerrainObjectPlacement TerrainProbePlacement
    TerrainSpawnPlan TerrainSpawnPlanCodec TerrainBiomePreset TerrainBiomePresetCodec
    TerrainMask TerrainPolygonMask TerrainCollisionMask TerrainNoiseMask TerrainImageMask TerrainBakedMaskCache
    TerrainEffect TerrainWaterField TerrainErosion TerrainSediment TerrainLegacyHydraulic TerrainLegacyThermal)
    list(APPEND _road_sources "${EVE_ROOT}/src/modules/procgen/heightmap/${_terrain_source}.cpp")
endforeach()
add_library(road_parity_core ${ROAD_PARITY_LINKAGE} ${_road_sources})
add_executable(road_parity_fixture Fixtures.cpp)
add_executable(road_authoring_fixture FunctionalFixtures.cpp OptionalFixtures.cpp)
add_executable(road_bundled_fixture BundledRoads.cpp)
target_link_libraries(road_bundled_fixture PRIVATE road_parity_core)
if(MSVC)
    set_property(TARGET road_bundled_fixture PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
endif()
add_executable(road_profile_test TestMain.cpp ${EVE_ROOT}/test/procgen_road_parameter_sampling.cpp ${EVE_ROOT}/test/procgen_spline_explicit_frames.cpp ${EVE_ROOT}/test/procgen_spline_path.cpp ${EVE_ROOT}/test/procgen_road_sampling.cpp ${EVE_ROOT}/test/procgen_road_sampling_defaults.cpp ${EVE_ROOT}/test/procgen_road_side_terrain.cpp ${EVE_ROOT}/test/procgen_road_roll_pivot.cpp ${EVE_ROOT}/test/procgen_road_circular.cpp ${EVE_ROOT}/test/procgen_road_circular_ghost.cpp ${EVE_ROOT}/test/procgen_road_controllers.cpp ${EVE_ROOT}/test/procgen_road_strength.cpp ${EVE_ROOT}/test/procgen_road_roll_policy.cpp ${EVE_ROOT}/test/procgen_road_full_roll.cpp ${EVE_ROOT}/test/procgen_road_recipe_snapshot.cpp ${EVE_ROOT}/test/procgen_road_presets.cpp ${EVE_ROOT}/test/procgen_road_reference.cpp ${EVE_ROOT}/test/procgen_road_spacing_portals.cpp ${EVE_ROOT}/test/procgen_road_extensions.cpp ${EVE_ROOT}/test/procgen_road_connections.cpp ${EVE_ROOT}/test/procgen_road_authoring.cpp ${EVE_ROOT}/test/procgen_road_terrain_blend.cpp ${EVE_ROOT}/test/procgen_road_profiles.cpp ${EVE_ROOT}/test/procgen_road_profile_editing.cpp ${EVE_ROOT}/test/procgen_road_editing.cpp)
target_link_libraries(road_parity_fixture PRIVATE road_parity_core)
target_link_libraries(road_authoring_fixture PRIVATE road_parity_core)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_surface_path.cpp)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_noop.cpp ${EVE_ROOT}/test/procgen_road_position_split.cpp ${EVE_ROOT}/test/procgen_road_junction_seams.cpp ${EVE_ROOT}/test/procgen_road_roundabout.cpp ${EVE_ROOT}/test/procgen_road_world_origin.cpp ${EVE_ROOT}/test/terrain_object_collision.cpp)
target_link_libraries(road_profile_test PRIVATE road_parity_core)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_bundled_circular.cpp)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_terrain_opacity.cpp)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_terrain_batch.cpp ${EVE_ROOT}/test/procgen_road_terrain_author_policy.cpp)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_geometry_sources.cpp)
target_sources(road_profile_test PRIVATE ${EVE_ROOT}/test/procgen_road_junction_style.cpp)
target_include_directories(road_parity_core PUBLIC
    ${EVE_ROOT}/src/engine ${EVE_ROOT}/src/modules
    ${_zero}/include ${_zero}/extension ${_zero}/third_party ${_zero_build})
if(MSVC)
    # Match the engine's strict warning and STL DLL-interface policy.
    target_compile_options(road_parity_core PUBLIC /utf-8 /EHsc /W4 /WX /wd4251 /wd4275)
    set_property(TARGET road_parity_core road_parity_fixture road_authoring_fixture road_profile_test PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
    target_link_libraries(road_parity_core PUBLIC "${_zero_build}/src/zeroerr.lib")
else()
    message(FATAL_ERROR "This local reference fixture harness currently requires the documented Windows MSVC host")
endif()

if(ROAD_PARITY_LINKAGE STREQUAL "SHARED")
    target_compile_definitions(road_parity_core PUBLIC EVENGINE_MODULE_DLL)
    target_compile_definitions(road_parity_core PRIVATE EVENGINE_ENGINE_EXPORTS EVENGINE_EXPORTS_DOMAINS EVENGINE_EXPORTS_FOUNDATION EVENGINE_EXPORTS_ORCHESTRATION EVENGINE_EXPORTS_PLATFORM)
endif()
enable_testing()
add_test(NAME road_profiles COMMAND road_profile_test "--testcase=.*(pointProfiles|authoring).*")
set_tests_properties(road_profiles PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed;Unknown exception")

# Exercise the production composite/store implementation in a focused static closure.
# The store and recipe factories have separate production translation units; no replacement stubs.
if(ROAD_PARITY_LINKAGE STREQUAL "STATIC")
    add_executable(road_artifact_test TestMain.cpp
        ${EVE_ROOT}/test/procgen_road_geometry_artifact.cpp
        ${EVE_ROOT}/test/procgen_road_recipe_artifact.cpp
        ${EVE_ROOT}/src/engine/common/ArtifactPublication.cpp
        ${EVE_ROOT}/src/modules/procgen/GeneratedArtifact.cpp
        ${EVE_ROOT}/src/modules/procgen/GeneratedArtifactMeshCodec.cpp
        ${EVE_ROOT}/src/modules/procgen/BuildKey.cpp
        ${EVE_ROOT}/src/modules/procgen/road/RoadArtifact.cpp)
    target_link_libraries(road_artifact_test PRIVATE road_parity_core)
    # Match the root MSVC warning policy for constexpr visitors / third-party headers.
    target_compile_options(road_artifact_test PRIVATE /wd4702)
    set_property(TARGET road_artifact_test PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDebugDLL)
    add_test(NAME road_author_artifact COMMAND road_artifact_test "--testcase=.*recipeArtifact.*")
    set_tests_properties(road_author_artifact PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed;Unknown exception")
endif()

add_test(NAME spline_contracts COMMAND road_profile_test "--testcase=procgen.splinePath.*")
set_tests_properties(spline_contracts PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed;Unknown exception")

add_test(NAME road_surface_path COMMAND road_profile_test "--testcase=procgen.road.surfacePath.*")
set_tests_properties(road_surface_path PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed;Unknown exception")

add_test(NAME road_noop COMMAND road_profile_test "--testcase=.*(editingNoOp|reconnectEndpointUndoRestoresOldJunctionConnections|connectAllTurnsIsAtomicAndUndoable).*")
set_tests_properties(road_noop PROPERTIES FAIL_REGULAR_EXPRESSION "Assertion Failed;Unknown exception")
