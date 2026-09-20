#pragma once

#include "common/Result.h"

namespace ssq { class Table; }
namespace eve::image { class ImageData; }
namespace eve::procgen {
class Heightmap;

/** @brief Range and write count returned by Pcg AnimationCurve texture baking. */
struct TerrainCurveTextureReceipt {
    float minimum = 0;
    float maximum = 0;
    int writtenPixels = 0;
};

/**
 * @brief Bake a sampled one-row curve into an existing one-row image with Pcg's exact pixel-coordinate contract.
 * @param output Exclusively borrowed mutable image; fully replaced only after successful evaluation.
 * @param curve Borrowed finite one-row curve LUT evaluated linearly over [0,1].
 * @return Curve range and destination width, or InvalidArgument without changing output.
 * @thread Caller serializes access to output and curve.
 * @reentrancy Does not invoke callbacks or retain references.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<TerrainCurveTextureReceipt> bakeTerrainCurveTexture(image::ImageData& output,
                                                                         const Heightmap& curve);
/** @brief Register the Pcg terrain curve texture adapter; VM-thread only. */
EVENGINE_API_DOMAINS void exposeTerrainCurveTexture(ssq::Table& table);
}  // namespace eve::procgen
