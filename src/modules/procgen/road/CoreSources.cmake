# CPU road boundary inventory, owned by the road implementation.
# The host module still discovers its sources through the regular module scan.
set(_eve_road_core_sources
    RoadAuthoring.cpp RoadConnection.cpp RoadNetwork.cpp RoadNetworkRecipes.cpp RoadNetworkSplit.cpp
    RoadProfile.cpp RoadSampling.cpp RoadSurfacePath.cpp RoadParameterSampling.cpp RoadBake.cpp RoadBakeEdges.cpp RoadBakeProfiles.cpp RoadBakeJunctions.cpp
    RoadBakeNavigation.cpp RoadBakePlacements.cpp RoadPlacementTerrain.cpp RoadBakeStructures.cpp RoadBakeFacilityLinks.cpp
    RoadTraffic.cpp RoadTerrain.cpp RoadTerrainWorkspace.cpp RoadTerrainWorkspaceCodec.cpp RoadGeometrySources.cpp RoadTerrainPlan.cpp
    RoadFacilityPrefab.cpp RoadArtifact.cpp RoadRecipes.cpp RoadNetworkCodec.cpp RoadRecipeNetwork.cpp RoadRecipeBake.cpp)
foreach(_eve_road_core_source IN LISTS _eve_road_core_sources)
  list(APPEND _eve_procgen_core_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/procgen/road/${_eve_road_core_source}")
endforeach()
