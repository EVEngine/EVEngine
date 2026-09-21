#pragma once

/** @file TerrainDetailCard.h @brief Canonical card geometry for texture-backed Unity terrain details. */

#include "asset/procgen/EvpackInstanceSetLoader.h"

#include <cstdint>
#include <vector>

namespace eve::asset_procgen {

/** @brief Owning unit geometry whose instance transform supplies Unity's exact width and height. */
struct TerrainDetailCardGeometry {
    std::vector<float>         positions;
    std::vector<float>         normals;
    std::vector<float>         texcoords;
    std::vector<std::uint32_t> indices;
    bool                       cameraFacing = false;
};

/**
 * @brief Build bottom-anchored unit cards for a texture-backed terrain Detail prototype.
 * @param prototype Borrowed immutable prototype metadata.
 * @return One camera-facing quad for GrassBillboard or two crossed quads for Grass.
 * @thread Worker-safe; allocates only detached CPU arrays and invokes no callbacks.
 * @remarks Mesh-backed and VertexLit prototypes return Unsupported so their authored mesh is never replaced.
 */
[[nodiscard]] Result<TerrainDetailCardGeometry> buildTerrainDetailCard(const RuntimeInstancePrototype& prototype);

}  // namespace eve::asset_procgen
