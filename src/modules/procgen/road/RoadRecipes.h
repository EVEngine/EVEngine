#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"

#include <memory>
#include <string>

namespace eve::image {
class ImageData;
}

namespace eve::procgen {
class MeshRecipeRegistry;
class TextureRecipeRegistry;

namespace road {

/** @brief Generate a multi-level interchange mesh from Params (span/height/lanes/seed). */
bool generateRoadNetworkMesh(const Params& params, MeshBuild& out, std::string& error);

/** @brief Bake navigation overlay for the same Params used by mesh.roadNetwork. */
[[nodiscard]] Result<RoadOverlay> generateRoadNetworkOverlay(const Params& params);

/** @brief Pure-function road marking atlas (asphalt + edge + dashed center + zebra). */
std::unique_ptr<image::ImageData> generateRoadMarkingsTexture(const Params& params, std::string& error);

void registerRoadMeshRecipes(MeshRecipeRegistry& registry);
void registerRoadTextureRecipes(TextureRecipeRegistry& registry);

}  // namespace road
}  // namespace eve::procgen
