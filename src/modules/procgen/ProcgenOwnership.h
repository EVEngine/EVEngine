#pragma once

#include "procgen/Procgen.h"
#include "procgen/MeshBuild.h"
#include "procgen/OutputSpec.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"
#include "procgen/texture/PbrMaterial.h"

#include "common/SquirrelOwnership.h"
#include "image/ImageData.h"

namespace eve::procgen {

/** @brief Module-owned registries behind Procgen generation handles. */
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
};

}  // namespace eve::procgen
