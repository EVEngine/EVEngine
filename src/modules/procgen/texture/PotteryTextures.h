#pragma once
#include <vector>
#include "procgen/texture/TextureRecipe.h"
namespace eve::procgen {
/**
 * @brief Create terracotta, glazed ceramic, porcelain, celadon and stoneware CPU texture definitions.
 * @return Owning definitions; no external resources or references are retained.
 * @thread Reentrant on any thread; definitions have no mutable shared state.
 */
[[nodiscard]] EVENGINE_API_DOMAINS std::vector<TextureRecipeDef> potteryTextureDefinitions();
}  // namespace eve::procgen
