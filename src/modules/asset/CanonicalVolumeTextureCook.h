#pragma once
#include "common/Export.h"

/** @file CanonicalVolumeTextureCook.h @brief Bounded R8 volume conversion to runtime EVVOL. */

#include "common/Result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::asset {

/** @brief Runtime definition and RGBA8 bulk generated from one canonical volume. */
struct CookedCanonicalVolumeTexture {
    std::vector<std::uint8_t> definition;
    std::vector<std::uint8_t> bulk;
};

/** @brief Validate volume-texture/1 and expand its X-fastest R8 voxels to RGBA8 EVVOL.
 * @param definition Canonical JSON definition bytes.
 * @param sourceR8 Exact raw R8 voxel blob referenced by the definition.
 * @param maximumDecodedBytes Upper bound including the EVVOL header and RGBA8 voxels.
 * @return Owning unpublished runtime candidate or structured failure.
 * @thread Worker-safe, synchronous and reentrant; no callbacks or retained pointers.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CookedCanonicalVolumeTexture> cookCanonicalVolumeTextureRgba8(
    std::span<const std::uint8_t> definition, std::span<const std::uint8_t> sourceR8,
    std::uint64_t maximumDecodedBytes);

}  // namespace eve::asset
