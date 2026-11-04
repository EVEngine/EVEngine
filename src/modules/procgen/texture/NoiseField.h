#pragma once
#include "common/Export.h"


#include <cstdint>

namespace eve::procgen {

/** @brief Distance metric for Worley / cellular noise. */
enum class WorleyMetric : uint8_t { Euclidean = 0, Manhattan = 1, Chebyshev = 2 };

/**
 * @brief Deterministic 2D noise helpers for pixel textures and masks.
 *
 * Covers the common NoiseCreator / noisecreater.com / Musgrave set:
 * white, value, Perlin, Simplex, Worley (F1/F2/edge/smooth + metrics),
 * fBm / ridged / billow / swiss / hybrid / jordan, Gabor, Perlin–Worley,
 * hex, gyroid, curl, value derivatives / isoline / banding, color-channel
 * fBm, and single- or multi-pass domain warp.
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
     * @brief Worley / cellular F1 distance (cell size 1).
     * @param jitter Feature-point jitter in [0, 1]; 1 = full cell, 0 = cell centers.
     * @param metric Euclidean (default), Manhattan, or Chebyshev.
     */
    float worleyF1(float x, float y, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;

    /** @brief Worley F2 (second-nearest) distance. */
    float worleyF2(float x, float y, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;

    /**
     * @brief Texture-friendly Worley: clamp(1 - F1, 0, 1).
     * Matches NoiseCreator's `1 - minDist` cellular look.
     */
    float worleyNoise(float x, float y, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;

    /** @brief Worley F2 - F1 edge distance (cell borders). */
    float worleyEdge(float x, float y, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;

    /** @brief (F1 + F2) * 0.5 remapped via clamp(1 - d, 0, 1). */
    float worleyCombine(float x, float y, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;

    /**
     * @brief Smooth-min Worley (metaball / soft cellular).
     * @param smoothness Blend width; larger → softer lobes. Result in [0, 1].
     */
    float worleySmooth(float x, float y, float smoothness = 0.25f, float jitter = 1.f) const;

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
     * @brief Musgrave Swiss turbulence: each octave warps by the running sum.
     * Good for eroded / alpine terrain. Result remapped to [0, 1].
     */
    float swiss(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f,
                float warpStrength = 0.15f) const;

    /**
     * @brief Musgrave hybrid multifractal.
     * @param offset Signal bias before weighting (typical ~0.7). Result in [0, 1].
     */
    float hybrid(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f,
                 float offset = 0.7f) const;

    /**
     * @brief Musgrave Jordan turbulence: warp by value-noise derivatives.
     * Completes the Swiss / Hybrid / Jordan Musgrave trio. Result in [0, 1].
     */
    float jordan(float x, float y, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f,
                 float warpStrength = 0.35f) const;

    /**
     * @brief Gabor / Voronoi-mix wavelet (noisecreater gavoronoi 3×3), remapped to [0, 1].
     * @param dirX / dirY Preferred wave direction bias.
     * @param frequency Carrier frequency (typical ~2π).
     */
    float gabor(float x, float y, float dirX = 0.f, float dirY = 0.f, float frequency = 6.2831853f) const;

    /** @brief Absolute-Gabor fBm remapped to [0, 1] (noisecreater Gabor swirl layers). */
    float gaborFbm(float x, float y, int octaves = 4, float dirX = 0.f, float dirY = 0.f) const;

    /**
     * @brief Perlin–Worley hybrid for cloud / soft cellular shapes.
     * Result is worleyNoise * fbmPerlin (NoiseCreator Cloud / Perlin-Worley).
     */
    float perlinWorley(float x, float y, int octaves = 3, float jitter = 1.f) const;

    /**
     * @brief ∂/∂x of value noise via central differences (slope / erosion helper).
     * Not remapped to [0, 1]; magnitude depends on `eps`.
     */
    float valueDx(float x, float y, float eps = 1e-3f) const;
    /** @brief ∂/∂y of value noise via central differences. */
    float valueDy(float x, float y, float eps = 1e-3f) const;
    /** @brief Gradient magnitude of value noise. */
    float valueSlope(float x, float y, float eps = 1e-3f) const;

    /**
     * @brief Contour / isoline mask of value noise in [0, 1] (1 on lines).
     * @param levels Contour count across [0, 1].
     * @param width Half-width of each line in normalized level space.
     */
    float isoLine(float x, float y, int levels = 8, float width = 0.04f) const;

    /**
     * @brief Quantized banding of value noise in [0, 1].
     * @param bands Number of discrete steps (≥ 1).
     */
    float banding(float x, float y, int bands = 5) const;

    /**
     * @brief Decorrelated fBm channel for RGB-style color noise (channel 0/1/2).
     * Same shape as fbm with fixed per-channel lattice offsets.
     */
    float fbmColor(float x, float y, int channel, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;

    /** @brief Single-pass domain warp of value fBm. */
    float warp(float x, float y, float amp, int octaves = 3) const;

    /**
     * @brief Two-pass iq-style domain warp (q then r), then sample fBm.
     * Matches noisecreater DomainWarp / Smoke nested fBm pattern.
     */
    float warpDeep(float x, float y, float amp, int octaves = 3) const;

    /**
     * @brief Divergence-free curl X of value-noise potential (finite differences).
     * Pair with curlNoiseY for particle / fluid advection.
     */
    float curlNoiseX(float x, float y, float eps = 1e-3f) const;
    /** @brief Divergence-free curl Y of value-noise potential (finite differences). */
    float curlNoiseY(float x, float y, float eps = 1e-3f) const;

    /**
     * @brief Distance to nearest hex edge (noisecreater hexDist field), typically [0, ~1].
     * Pointy-top lattice; scale inputs to control cell size.
     */
    float hexDistance(float x, float y) const;

    /** @brief Hash of the nearest hex cell, in [0, 1]. */
    float hexValue(float x, float y) const;

    /**
     * @brief Hex edge line mask in [0, 1] (1 on edge).
     * Matches noisecreater hexNoise edge band.
     */
    float hexEdge(float x, float y, float width = 0.05f, float soft = 0.02f) const;

    /**
     * @brief Minimal gyroid field at (x, y, z): dot(sin(p), cos(p.yzx)), remapped to [0, 1].
     */
    float gyroid(float x, float y, float z) const;

    /** @brief Absolute-gyroid fBm (noisecreater gyroid fbm), remapped to [0, 1]. */
    float gyroidFbm(float x, float y, float z, int octaves = 4) const;
};

/**
 * @brief Deterministic 3D noise helpers for volumes / density fields.
 * When period* > 0, lattice indices wrap for seamless tiling.
 */
struct EVENGINE_API_DOMAINS NoiseField3 {
    uint32_t seed    = 1;
    int      periodX = 0;
    int      periodY = 0;
    int      periodZ = 0;

    float hash01(int ix, int iy, int iz) const;
    float whiteNoise(int ix, int iy, int iz) const;
    float valueNoise(float x, float y, float z) const;
    float perlinNoise(float x, float y, float z) const;
    float simplexNoise(float x, float y, float z) const;
    float worleyF1(float x, float y, float z, float jitter = 1.f, WorleyMetric metric = WorleyMetric::Euclidean) const;
    float worleyNoise(float x, float y, float z, float jitter = 1.f,
                      WorleyMetric metric = WorleyMetric::Euclidean) const;
    float fbm(float x, float y, float z, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float fbmPerlin(float x, float y, float z, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float ridged(float x, float y, float z, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float billow(float x, float y, float z, int octaves = 4, float lacunarity = 2.f, float gain = 0.5f) const;
    float gyroid(float x, float y, float z) const;
    float gyroidFbm(float x, float y, float z, int octaves = 4) const;
    float perlinWorley(float x, float y, float z, int octaves = 3, float jitter = 1.f) const;
};

}  // namespace eve::procgen
