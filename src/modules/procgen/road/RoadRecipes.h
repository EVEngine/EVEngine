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

/**
 * @brief Build the shared road recipe once for mesh, navigation and artifact projections.
 * @param params
 * Deterministic scene and bake parameters. Terrain-snapped placements require a nonempty
 * placementWorldRevision identifying the query world's content and selection rules; the caller must
 * change it whenever projection results can change and keep that world stable throughout the call.
 * This dependency participates in the artifact BuildKey through canonical Params.
 * @return Owning bake products, or a structured validation/generation
 * error.
 * @cost Linear in sampled road geometry and enabled overlays; reuse the returned products for multiple
 * projections.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadBakeResult> bakeRoadNetworkRecipe(const Params& params);

/** @brief Generate a multi-level interchange mesh from Params (span/height/lanes/seed). */
bool generateRoadNetworkMesh(const Params& params, MeshBuild& out, std::string& error);

/** @brief Bake navigation overlay for the same Params used by mesh.roadNetwork. */
[[nodiscard]] Result<RoadOverlay> generateRoadNetworkOverlay(const Params& params);

/** @brief Pure-function road marking atlas (asphalt + edge + dashed center + zebra). */
std::unique_ptr<image::ImageData> generateRoadMarkingsTexture(const Params& params, std::string& error);

/** @brief Registers road mesh recipes. */
void registerRoadMeshRecipes(MeshRecipeRegistry& registry);
/** @brief Registers road texture recipes. */
void registerRoadTextureRecipes(TextureRecipeRegistry& registry);

}  // namespace road
}  // namespace eve::procgen
