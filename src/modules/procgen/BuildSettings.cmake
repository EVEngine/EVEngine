eve_module_enabled(procgen _has_procgen)
option(EVENGINE_BUILD_PROCGEN_CORE_CHECK
  "Compile procgen CPU core sources without graphics/map/scene adapters" OFF)
set(_eve_procgen_core_profile FALSE)
if(EVENGINE_PROFILE STREQUAL "procgen-core-only")
  set(_eve_procgen_core_profile TRUE)
endif()
if(_has_procgen OR _eve_procgen_core_profile OR EVENGINE_BUILD_PROCGEN_CORE_CHECK)
  if(_has_procgen)
    # Compat shims first (Apple Clang: missing drawOperatorArray in upstream DTL),
    # then DTL. BEFORE must precede the visibility keyword so -I dtl_compat wins
    # over the submodule.
    target_include_directories(EVProcgen BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/procgen/dtl_compat)
    target_include_directories(EVProcgen PUBLIC
      ${CMAKE_SOURCE_DIR}/third-party/DungeonTemplateLibrary/include)
  endif()

  # Procgen's CPU data and recipe path is compiled separately as a dependency
  # boundary check. This is intentionally an OBJECT target: it proves that the
  # selected core translation units compile without linking graphics, map or
  # scene adapters, while the normal EVProcgen target keeps the public facade
  # and all compatibility implementations unchanged.
  set(_eve_procgen_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/ArrowGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/PcgSpawnProgress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ArtifactPublish.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/AttributeTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/core/ProcgenCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainStamp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainMeshStamp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainDetailLayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainTreePlacement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainSpawnPlan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainSpawnPlanCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainBiomePreset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainBiomePresetCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainCollisionMask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainPolygonMask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainProbePlacement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainNoiseMask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainSplatmap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainWorldWorkspace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainWorldRoad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainGenerationSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/PcgRuntimeStamper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainMask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainImageMask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainEffect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainErosion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainLegacyHydraulic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainLegacyThermal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainSediment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainWaterField.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/heightmap/TerrainSurface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratorRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratedArtifact.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/BuildKey.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratedArtifactRecipes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Grid2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/JsonExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/MeshBuild.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshDeformationSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshBoolean.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshContactBlend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshMerge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshAdhereLive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshModifierGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshUvProjection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/spline/SplineCircularArc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/spline/SplinePath.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Palette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ParamSchema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/PointSet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/PointDelta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ProcgenSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/RuntimeGeneration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/RuntimeGenerationAsset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/Semantic.cpp)
  include("${CMAKE_CURRENT_SOURCE_DIR}/procgen/road/CoreSources.cmake")
  set(_eve_procgen_core_boundary_files ${_eve_procgen_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/ArrowGenerator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/ArtifactPublish.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/GeneratedArtifact.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshDeformationSession.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshBoolean.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshContactBlend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshMerge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshAdhereLive.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshModifierGraph.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/mesh/MeshUvProjection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/spline/SplinePath.h
    ${CMAKE_CURRENT_SOURCE_DIR}/procgen/core/ProcgenCore.h)
  foreach(_eve_procgen_core_file IN LISTS _eve_procgen_core_boundary_files)
    if(NOT EXISTS "${_eve_procgen_core_file}")
      message(FATAL_ERROR "Procgen core source is missing: ${_eve_procgen_core_file}")
    endif()
    file(READ "${_eve_procgen_core_file}" _eve_procgen_core_content)
    if(_eve_procgen_core_content MATCHES "include[ \t]*[\"<](graphics|map|scene)/")
      message(FATAL_ERROR
        "Procgen core source must not include a presentation adapter: ${_eve_procgen_core_file}")
    endif()
  endforeach()
  if(EVENGINE_BUILD_PROCGEN_CORE_CHECK OR _eve_procgen_core_profile)
    add_library(EVProcgenCoreCheck OBJECT ${_eve_procgen_core_sources})
    target_link_libraries(EVProcgenCoreCheck PRIVATE eve_engine_includes)
    if(EVENGINE_MODULE_LINKAGE STREQUAL "SHARED")
      # This check compiles implementations, rather than consuming the domain DLL.
      target_compile_definitions(EVProcgenCoreCheck PRIVATE EVENGINE_EXPORTS_DOMAINS)
    endif()
    target_include_directories(EVProcgenCoreCheck PRIVATE
      ${CMAKE_SOURCE_DIR}/src/modules)
    set_target_properties(EVProcgenCoreCheck PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

