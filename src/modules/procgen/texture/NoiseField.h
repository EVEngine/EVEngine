#pragma once
#include "common/Export.h"


#include <cstdint>

namespace eve::procgen {

/**
 * @brief Deterministic value-noise helpers for pixel textures.
 * When periodX/periodY > 0, lattice wraps for seamless tiling.
 */
struct EVENGINE_API_DOMAINS NoiseField {
    uint32_t seed     = 1;
    int      periodX  = 0;  // 0 = non-tiling
    int      periodY  = 0;

    /** @brief Hash 01. */
    float hash01(int ix, int iy) const;
    /** @brief Value noise. */
    float valueNoise(float x, float y) const;
    /** @brief Classic 2D Perlin gradient noise, remapped to [0, 1]. */
    float perlinNoise(float x, float y) const;
    /** @brief Fbm. */
    float fbm(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    /** @brief Fbm perlin. */
    float fbmPerlin(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    /** @brief Ridged. */
    float ridged(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    /** @brief Ridged perlin. */
    float ridgedPerlin(float x, float y, int octaves = 4, float lacunarity = 2.f,
                       float gain = 0.5f) const;
    /** @brief Warp. */
    float warp(float x, float y, float amp, int octaves = 3) const;
};

}  // namespace eve::procgen
