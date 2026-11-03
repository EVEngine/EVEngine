#pragma once
#include "common/Export.h"


#include <cstdint>

namespace eve::procgen {

/**
 * @brief Deterministic 2D noise helpers for pixel textures and masks.
 *
 * Algorithms align with the common NoiseCreator / noisecreater.com set:
 * white, value, Perlin, Simplex, Worley (F1 / edge), fBm / ridged / billow,
 * Perlin–Worley (cloud), and single- or multi-pass domain warp.
 *
 * When periodX/periodY > 0, lattice indices wrap for seamless tiling.
 * Continuous samplers return values in [0, 1] unless noted.
 */
struct EVENGINE_API_DOMAINS NoiseField {
    uint32_t seed    = 1;
    int      periodX = 0;  // 0 = non-tiling
    int      periodY = 0;

    /** @brief Lattice hash mapped to [0, 1]. */
    float hash01(int ix, int iy) const;

    /** @brief White noise at integer lattice point (alias of hash01). */
    float whiteNoise(int ix, int iy) const;

    /** @brief Smooth value noise. */
    float valueNoise(float x, float y) const;

    /** @brief Classic 2D Perlin gradient noise, remapped to [0, 1]. */
    float perlinNoise(float x, float y) const;

    /**
     * @brief 2D Simplex noise remapped to [0, 1].
     * Same family as noisecreater SampleNoiseA (Ashima-style corners + radial falloff).
     */
    float simplexNoise(float x, float y) const;

    /**
     * @brief Worley / cellular F1 distance (cell size 1), typically in [0, ~1.5].
     * @param jitter Feature-point jitter in [0, 1]; 1 = full cell, 0 = cell centers.
     */
    float worleyF1(float x, float y, float jitter = 1.f) const;

    /**
     * @brief Texture-friendly Worley: clamp(1 - F1, 0, 1).
     * Matches NoiseCreator's `1 - minDist` cellular look.
     */
    float worleyNoise(float x, float y, float jitter = 1.f) const;

    /** @brief Worley F2 - F1 edge distance (cell borders). */
    float worleyEdge(float x, float y, float jitter = 1.f) const;

    /** @brief Value-noise fBm. */
    float fbm(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Perlin fBm with uncorrelated octave offsets. */
    float fbmPerlin(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Simplex fBm with uncorrelated octave offsets. */
    float fbmSimplex(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /**
     * @brief Rotated value-noise fBm (axial-bias reduction).
     * Each octave applies a fixed 2D rotation like noisecreater smoke / domain-warp fBm.
     */
    float fbmRotated(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Ridged multifractal on value noise. */
    float ridged(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Ridged multifractal on Perlin. */
    float ridgedPerlin(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /**
     * @brief Billow / turbulence (absolute value-noise fBm), remapped to [0, 1].
     * Matches noisecreater SampleNoiseB `noise_sum_abs` shape.
     */
    float billow(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Billow built from Perlin octaves. */
    float billowPerlin(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /**
     * @brief Perlin–Worley hybrid for cloud / soft cellular shapes.
     * Result is worleyNoise * fbmPerlin (NoiseCreator Cloud / Perlin-Worley).
     */
    float perlinWorley(float x, float y, int octaves = 3, float jitter = 1.f) const;

    /** @brief Single-pass domain warp of value fBm. */
    float warp(float x, float y, float amp, int octaves = 3) const;

    /**
     * @brief Two-pass iq-style domain warp (q then r), then sample fBm.
     * Matches noisecreater DomainWarp / Smoke nested fBm pattern.
     */
    float warpDeep(float x, float y, float amp, int octaves = 3) const;
};

}  // namespace eve::procgen
