#pragma once

#include "procgen/Procgen.h"

#include "procgen/Biome.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/Grid2D.h"
#include "procgen/GridGraph.h"
#include "procgen/MeshBuild.h"
#include "procgen/MeshGraph.h"
#include "procgen/OutputSpec.h"
#include "procgen/Palette.h"
#include "procgen/Params.h"
#include "procgen/PointGraph.h"
#include "procgen/PointSet.h"
#include "procgen/ProcgenSystem.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/algorithms/LSystem.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/MeshDeformationSession.h"
#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/spline/SplinePath.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"
#include "procgen/texture/PbrMaterial.h"

#include "common/RuntimeRegistry.h"
#include "image/ImageData.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief Module-owned registries and mutable caches behind the Procgen facade. */
struct Procgen::OwnershipState {
    eve::script::RuntimeObjectRegistry<OutputSpec, ProcgenOutputHandleTag>             outputs;
    eve::script::RuntimeObjectRegistry<PointSet, ProcgenPointSetHandleTag>             points;
    eve::script::RuntimeObjectRegistry<TerrainSampler, ProcgenTerrainSamplerHandleTag> samplers;
    eve::script::RuntimeObjectRegistry<Heightmap, ProcgenHeightmapHandleTag>           heightmaps;
    eve::script::RuntimeObjectRegistry<CloudField, ProcgenCloudFieldHandleTag>         clouds;
    eve::script::RuntimeObjectRegistry<CloudShadow, ProcgenCloudShadowHandleTag>       shadows;
    eve::script::RuntimeObjectRegistry<PbrTextureSet, ProcgenPbrMaterialHandleTag>     pbr;
    eve::script::RuntimeObjectRegistry<MeshBuild, ProcgenMeshBuildHandleTag>           meshes;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenImageHandleTag>        images;
    eve::script::RuntimeObjectRegistry<image::ImageData, ProcgenNormalImageHandleTag>  normalImages;

    eve::script::RuntimeObjectRegistry<Params, ProcgenParamsHandleTag>                       params;
    eve::script::RuntimeObjectRegistry<Grid2D, ProcgenGridHandleTag>                         grids;
    eve::script::RuntimeObjectRegistry<ProcgenContext, ProcgenContextHandleTag>              contexts;
    eve::script::RuntimeObjectRegistry<SpatialData, ProcgenSpatialDataHandleTag>             spatialData;
    eve::script::RuntimeObjectRegistry<RuntimeGeneration, ProcgenRuntimeGenerationHandleTag> runtimeGenerations;
    eve::script::RuntimeObjectRegistry<PointGraph, ProcgenPointGraphHandleTag>               pointGraphs;
    eve::script::RuntimeObjectRegistry<MeshModifierGraph, ProcgenMeshModifierGraphHandleTag> meshModifierGraphs;
    eve::script::RuntimeObjectRegistry<MeshDeformationSession, ProcgenMeshDeformationSessionHandleTag>
        meshDeformationSessions;
    eve::script::RuntimeObjectRegistry<DynamicMeshUvPaintSession, ProcgenDynamicMeshUvPaintSessionHandleTag>
                                                                                   dynamicMeshUvPaintSessions;
    eve::script::RuntimeObjectRegistry<SplinePath, ProcgenSplinePathHandleTag>     splinePaths;
    eve::script::RuntimeObjectRegistry<GridGraph, ProcgenGridGraphHandleTag>       gridGraphs;
    eve::script::RuntimeObjectRegistry<MeshGraph, ProcgenMeshGraphHandleTag>       meshGraphs;
    eve::script::RuntimeObjectRegistry<BiomeRules, ProcgenBiomeRulesHandleTag>     biomeRules;
    eve::script::RuntimeObjectRegistry<ShapeGrammar, ProcgenShapeGrammarHandleTag> shapeGrammars;
    eve::script::RuntimeObjectRegistry<LSystem, ProcgenLSystemHandleTag>           lsystems;

    PaletteTable                                           palettes;
    std::unordered_map<std::string, ProcgenSystemSnapshot> systems;
    std::unordered_map<std::string, ProcgenSystemSnapshot> previousSystems;
    ArtifactStore                                          artifactStore;
    std::uint64_t                                          nextArtifactSequence = 1;
    /** Scratch buffer for legacy generate/palette paths that still take a string* error out-param. */
    mutable std::string              compatMessage;
    mutable std::vector<std::string> algorithmIdsCache;
    mutable std::vector<std::string> textureRecipeIdsCache;
    mutable std::vector<std::string> pbrRecipeIdsCache;
    mutable std::vector<std::string> meshRecipeIdsCache;
};

}  // namespace eve::procgen
