#pragma once

#include <cstdint>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
enum class TerrainMaskBlend;

/** @brief Pcg NoiseLib families available to the native scalar mask generator. */
enum class TerrainNoiseType { Perlin, Value, Billow, Ridge, Voronoi };

/** @brief Value-only 2D NoiseMask transform, fBm, optional domain-warp and deterministic-stream settings. */
struct TerrainNoiseMaskSettings {
    float translationX = 0, translationZ = 0;
    float scaleX = 10, scaleZ = 10, rotation = 0;
    float octaves = 8, amplitude = 0.5F, frequency = 1, persistence = 0.5F, lacunarity = 2;
    float warpIterations = 0, warpStrength = 0.5F, warpOffsetX = 2.5F, warpOffsetZ = 1.4F;
    uint32_t seed = 0;
    TerrainNoiseType type = TerrainNoiseType::Perlin;
};

/**
 * @brief Generate a transformed Pcg-style fBm noise source, curve it, then combine it with an input mask.
 * @param target Exclusively borrowed finite destination matching input; aliases are permitted.
 * @param input Borrowed current mask stack value.
 * @param curve Borrowed finite nonempty one-row strength curve.
 * @param settings Finite transform/fractal controls; scale nonzero, octaves/warpIterations in [0,16].
 * @param mode Multiply, maximum, minimum, add or subtract.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; seed is explicit and no state, callback, time or RNG is retained.
 * Rotation is radians in the X/Z plane. Fractional octave and warp counts interpolate the final step.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateTerrainNoiseMask(Heightmap& target, const Heightmap& input,
                                                                        const Heightmap&                curve,
                                                                        const TerrainNoiseMaskSettings& settings,
                                                                        TerrainMaskBlend                mode);
}  // namespace eve::procgen
