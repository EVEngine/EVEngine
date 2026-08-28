#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <span>
#include <string_view>

namespace eve::procgen {

class MeshRecipeRegistry;

/** @brief Metadata for one centered, Y-up module in the built-in prototype construction kit. */
struct PrototypePieceDescriptor {
    std::string_view id;             ///< Stable recipe suffix without the `prototype.` prefix.
    std::string_view displayName;    ///< Human-readable name for tools and editors.
    float            defaultWidth;   ///< Default X extent in world units.
    float            defaultHeight;  ///< Default Y extent in world units.
    float            defaultDepth;   ///< Default Z extent in world units.
};

/**
 * @brief Return the stable catalogue of 75 procedural prototype modules.
 * @return Process-lifetime read-only descriptor span.
 * @ownership Borrowed static storage; never retain a pointer beyond process lifetime.
 * @thread Safe for concurrent reads.
 * @reentrancy Does not invoke callbacks.
 */
std::span<const PrototypePieceDescriptor> prototypePieceDescriptors() noexcept;

/**
 * @brief Build one prototype module as an owning CPU triangle mesh.
 *
 * All pieces use a centered XZ footprint, a ground-level Y origin, and stable
 * named triangle groups. Common parameters are `scale`, `width`, `height`,
 * `depth`, `thickness`, `detail`, `steps`, and `uvScale` (texture tiles per
 * world unit); omitted dimensions use the
 * descriptor defaults. Generation is deterministic and has no file/resource
 * dependency.
 *
 * @param pieceId Stable id from prototypePieceDescriptors().
 * @param params Owning generation parameters borrowed for this call only.
 * @return Generated mesh or a structured InvalidArgument/NotFound diagnostic.
 * @thread Safe; the function mutates no shared state.
 * @reentrancy Does not invoke external callbacks.
 */
[[nodiscard]] eve::Result<MeshBuild> generatePrototypePiece(std::string_view pieceId, const Params& params);

/**
 * @brief Register all prototype modules as `prototype.*` mesh recipes.
 * @param registry Registry borrowed for this synchronous call; it retains copied descriptors and callbacks.
 * @thread Call only during single-threaded registry initialization.
 * @reentrancy Does not invoke registered callbacks.
 */
void registerPrototypePieceRecipes(MeshRecipeRegistry& registry);

}  // namespace eve::procgen
