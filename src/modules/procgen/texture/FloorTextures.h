#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "procgen/Params.h"

#include <memory>
#include <string>

namespace eve::image {
class ImageData;
}

namespace eve::procgen {

class TextureRecipeRegistry;
class PbrRecipeRegistry;
struct PbrTextureSet;

/**
 * @brief Generate a parameterized wood-floor albedo (`tex.floor.wood`).
 * @ownership On success the caller exclusively owns the returned ImageData.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<image::ImageData>> generateWoodFloorTexture(
    const Params& params);

/**
 * @brief Generate a parameterized ceramic / patterned floor-tile albedo (`tex.floor.tile`).
 * @ownership On success the caller exclusively owns the returned ImageData.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<image::ImageData>> generateTileFloorTexture(
    const Params& params);

/**
 * @brief Generate a full PBR map set for wood floors (`pbr.floor.wood`).
 * @ownership On success the caller exclusively owns the returned set and all maps.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<PbrTextureSet>> generateWoodFloorPbr(
    const Params& params);

/**
 * @brief Generate a full PBR map set for ceramic / patterned tiles (`pbr.floor.tile`).
 * @ownership On success the caller exclusively owns the returned set and all maps.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<PbrTextureSet>> generateTileFloorPbr(
    const Params& params);

/** @brief Register `tex.floor.wood` and `tex.floor.tile` with schema metadata. */
EVENGINE_API_DOMAINS void registerFloorTextureRecipes(TextureRecipeRegistry& registry);

/** @brief Register `pbr.floor.wood` and `pbr.floor.tile` with schema metadata. */
EVENGINE_API_DOMAINS void registerFloorPbrRecipes(PbrRecipeRegistry& registry);

}  // namespace eve::procgen
