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
 * @brief Generate a parameterized steel-cable braid albedo (`tex.cable.steel`).
 * @ownership On success the caller exclusively owns the returned ImageData.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<image::ImageData>> generateSteelCableTexture(
    const Params& params);

/**
 * @brief Generate a parameterized iron / rusted metal albedo (`tex.chain.iron`).
 * @ownership On success the caller exclusively owns the returned ImageData.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<image::ImageData>> generateIronChainTexture(
    const Params& params);

/**
 * @brief Generate a parameterized hemp-rope fiber albedo (`tex.rope.hemp`).
 * @ownership On success the caller exclusively owns the returned ImageData.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<image::ImageData>> generateHempRopeTexture(
    const Params& params);

/**
 * @brief Full PBR map set for steel cable (`pbr.cable.steel`).
 * @ownership On success the caller exclusively owns the returned set and all maps.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<PbrTextureSet>> generateSteelCablePbr(
    const Params& params);

/**
 * @brief Full PBR map set for iron chain metal (`pbr.chain.iron`).
 * @ownership On success the caller exclusively owns the returned set and all maps.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<PbrTextureSet>> generateIronChainPbr(
    const Params& params);

/**
 * @brief Full PBR map set for hemp rope (`pbr.rope.hemp`).
 * @ownership On success the caller exclusively owns the returned set and all maps.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<std::unique_ptr<PbrTextureSet>> generateHempRopePbr(
    const Params& params);

/** @brief Register `tex.cable.steel` / `tex.chain.iron` / `tex.rope.hemp`. */
EVENGINE_API_DOMAINS void registerCableTextureRecipes(TextureRecipeRegistry& registry);

/** @brief Register `pbr.cable.steel` / `pbr.chain.iron` / `pbr.rope.hemp`. */
EVENGINE_API_DOMAINS void registerCablePbrRecipes(PbrRecipeRegistry& registry);

}  // namespace eve::procgen
