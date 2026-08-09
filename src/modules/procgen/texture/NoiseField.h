#pragma once

#include <cstdint>

namespace eve::procgen {

/**
 * Deterministic value-noise helpers for pixel textures.
 * When periodX/periodY > 0, lattice wraps for seamless tiling.
 */
struct NoiseField {
    uint32_t seed     = 1;
    int      periodX  = 0;  // 0 = non-tiling
    int      periodY  = 0;

    float hash01(int ix, int iy) const;
    float valueNoise(float x, float y) const;
    float fbm(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float ridged(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float warp(float x, float y, float amp, int octaves = 3) const;
};

}  // namespace eve::procgen
