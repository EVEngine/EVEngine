#pragma once
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
enum class TerrainMaskBlend;
/** @brief Reference image selection modes; MaximumRgb is not luminance. */
enum class TerrainImageFilter { MaximumRgb, ColorSelection, Red, Green, Blue, Alpha };
/** @brief Image UV transform and color-selection settings, rotation in radians. */
struct TerrainImageMaskSettings {
    float              offsetX = 0, offsetZ = 0, scaleX = 1, scaleZ = 1, rotation = 0;
    float              red = 1, green = 1, blue = 1, accuracy = 0;
    bool               tiling = false;
    TerrainImageFilter filter = TerrainImageFilter::MaximumRgb;
};
/**
 * @brief Generate the reference image-mask filter from borrowed RGBA scalar planes.
 * @param target Exclusively borrowed finite output, same dimensions as input; aliases allowed.
 * @param input Borrowed finite base mask.
 * @param red Borrowed finite red plane; all four color planes must share dimensions.
 * @param green Borrowed finite green plane.
 * @param blue Borrowed finite blue plane.
 * @param alpha Borrowed finite alpha plane; colors are supplied explicitly without hidden color-space conversion.
 * @param curve Borrowed finite nonempty one-row strength curve.
 * @param settings Finite UV/color settings, nonzero scales and accuracy in [0,1].
 * @param mode Multiply, maximum, minimum, add or subtract after curve lookup.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive output access, immutable snapshots, no callbacks or retained references.
 * Samples texture centers, wraps with source negative-integer convention when tiling,
 * and returns filter zero outside the footprint BEFORE curve lookup. ColorSelection uses
 * the source sRGB-to-Lab/CIE76 equations and strict threshold; accuracy=1 rejects even exact matches.
 * No implicit strength or inversion is applied. Image decoding and GPU resources remain caller-owned.
 */
[[nodiscard]] Result<int> generateTerrainImageMask(Heightmap& target, const Heightmap& input, const Heightmap& red,
                                                   const Heightmap& green, const Heightmap& blue,
                                                   const Heightmap& alpha, const Heightmap& curve,
                                                   const TerrainImageMaskSettings& settings, TerrainMaskBlend mode);

/**
 * @brief Apply a scalar GlobalSpawnerMaskStack output through Pcg's image-mask transform, curve and blend pass.
 * @param target Exclusively borrowed output matching input; aliases with input/source/curve are permitted.
 * @param input Borrowed current ordered mask-stack value.
 * @param source Borrowed finite scalar output of the referenced global spawner mask stack.
 * @param curve Borrowed finite nonempty one-row strength curve.
 * @param settings Image-mask UV transform; filter is ignored because the scalar source is replicated to RGBA.
 * @param mode Multiply, maximum, minimum, add or subtract.
 * @return Changed count or InvalidArgument atomically.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; no retained resources, callbacks, time, or RNG.
 */
[[nodiscard]] Result<int> applyTerrainGlobalSpawnerMask(Heightmap& target, const Heightmap& input,
                                                        const Heightmap& source, const Heightmap& curve,
                                                        const TerrainImageMaskSettings& settings,
                                                        TerrainMaskBlend mode);
}  // namespace eve::procgen
