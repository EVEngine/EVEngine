#pragma once

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;

/** @brief Ordered scalar-mask arithmetic; intermediate values are not clamped. */
enum class TerrainMaskBlend { Multiply, Maximum, Minimum, Add, Subtract };

/** @brief Native height operations, in terrain height units (not Unity packed heights). */
enum class TerrainStampOperation { Raise, Lower, Blend, Set, Add, Subtract };

/**
 * @brief Value configuration for a rectangular stamp in world X/Z coordinates.
 *
 * Terrain sample (x,y) is at (originX+x*spacingX, originZ+y*spacingZ).
 * Stamp width/depth span its first and last texel. Positive rotation is
 * counterclockwise in the X/Z plane; radians. No time or randomness is used.
 * Heights are finite, unbounded world heights, not normalized/packed values.
 */
struct TerrainStampSettings {
    double                originX = 0, originZ = 0;
    double                spacingX = 1, spacingZ = 1;
    double                centerX = 0, centerZ = 0;
    double                width = 1, depth = 1, rotation = 0;
    float                 amplitude = 1, baseHeight = 0;
    float                 blendStrength = 0.5F;
    TerrainStampOperation operation     = TerrainStampOperation::Raise;
};

/**
 * @brief Blend one scalar raster into an existing mask stack, in call order.
 * @param target Borrowed writable mask; must have the same dimensions as source.
 * @param source Borrowed finite scalar raster; may alias target.
 * @param mode Arithmetic applied before interpolating from the old target by strength.
 * @param strength Finite interpolation weight in [0,1].
 * @param invert Use 1-source before blending; does not invert the old target.
 * @return Changed sample count, or InvalidArgument without modifying target.
 * @throws std::bad_alloc Allocation failure; target remains unchanged.
 * @thread Caller exclusively owns target during this synchronous call. No callbacks,
 * retained references, renderer, or ECS state. Repeatable on one toolchain;
 * cross-platform floating-point results are compared with tolerance.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> blendTerrainMask(Heightmap& target, const Heightmap& source,
                                                                TerrainMaskBlend mode, float strength = 1,
                                                                bool invert = false);

/**
 * @brief Apply a rotated, bilinearly sampled stamp; publish only after all checks succeed.
 * @param target Borrowed mutable terrain; unchanged on invalid input or non-finite output.
 * @param stamp Borrowed finite height raster; may alias target.
 * @param settings World placement and height operation; positive finite spacing/extents required.
 * @param localMask Borrowed raster, sampled in stamp UVs; multiplies stamp height before amplitude/base.
 * @param globalMask Borrowed raster, sampled in stamp UVs; clamps to [0,1] and weights the final height change.
 * Both masks may have independent resolutions; a 1x1 raster containing 1 disables masking.
 * Outside the rotated stamp rectangle, target is unchanged. Raise/Lower take
 * max/min with baseHeight+amplitude*stamp*localMask; Set uses that level; Blend
 * interpolates to it by blendStrength; Add/Subtract add/subtract that level.
 * Mask arithmetic is native scalar arithmetic: Unity adaptive-base, packed-height
 * clamping, curve textures and MixHeight are not implemented by this API.
 * @return Changed sample count, or InvalidArgument without modifying target.
 * @throws std::bad_alloc Allocation failure; target remains unchanged.
 * @thread Exclusive target access; all other inputs immutable for this synchronous
 * call. No borrowed data is retained and no callbacks are invoked. No RNG/time.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> applyTerrainStamp(Heightmap& target, const Heightmap& stamp,
                                                                 const TerrainStampSettings& settings,
                                                                 const Heightmap&            localMask,
                                                                 const Heightmap&            globalMask);
}  // namespace eve::procgen
