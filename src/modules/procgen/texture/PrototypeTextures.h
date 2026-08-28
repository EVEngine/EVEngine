#pragma once

#include "common/Result.h"
#include "image/ImageData.h"
#include "procgen/Params.h"

#include <memory>
#include <span>
#include <string_view>

namespace eve::procgen {

class TextureRecipeRegistry;

/** @brief Metadata for one development/prototype texture pattern. */
struct PrototypeTextureDescriptor {
    std::string_view id;           ///< Stable recipe suffix without the `tex.prototype.` prefix.
    std::string_view displayName;  ///< Human-readable name for tools and editors.
};

/**
 * @brief Return the stable catalogue of 13 procedural prototype patterns.
 * @return Process-lifetime read-only descriptor span.
 * @ownership Borrowed static storage; never retain a pointer beyond process lifetime.
 * @thread Safe for concurrent reads.
 * @reentrancy Does not invoke callbacks.
 */
std::span<const PrototypeTextureDescriptor> prototypeTextureDescriptors() noexcept;

/**
 * @brief Generate a parameterized RGBA8 prototype texture without loading an image asset.
 * @ownership On success the caller exclusively owns the returned ImageData.
 * @lifetime The returned image remains valid until the caller releases it; params are borrowed only for this call.
 *
 * Parameters include the dedicated Params width/height, `palette`
 * (dark, light, purple, orange, green, red, custom), `cellSize`, `lineWidth`,
 * `minorAlpha`, `majorAlpha`, and custom background and line RGB channels.
 * Every pattern is deterministic and tile-safe where its guide geometry permits.
 *
 * @param patternId Stable id from prototypeTextureDescriptors().
 * @param params Owning parameters borrowed for this synchronous call.
 * @return Uniquely owned image or a structured diagnostic.
 * @thread Safe; no shared state is mutated.
 * @reentrancy Does not invoke external callbacks.
 */
[[nodiscard]] eve::Result<std::unique_ptr<image::ImageData>> generatePrototypeTexture(std::string_view patternId,
                                                                                      const Params&    params);

/**
 * @brief Register all patterns as `tex.prototype.*` texture recipes.
 * @param registry Registry borrowed for this synchronous call; it retains copied descriptors and callbacks.
 * @thread Call only during single-threaded registry initialization.
 * @reentrancy Does not invoke registered callbacks.
 */
void registerPrototypeTextureRecipes(TextureRecipeRegistry& registry);

}  // namespace eve::procgen
