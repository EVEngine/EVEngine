#pragma once

#include "procgen/Params.h"
#include "procgen/texture/NoiseField.h"

#include <cstdint>
#include <functional>

namespace eve::procgen {

/**
 * @brief Deterministic terrain height sampling (Red Blob Games / Perlin fBm recipe).
 *
 * `sample(x, y)` maps any continuous map coordinate (tile/world units) to a
 * height in [0, 1] by default. Same coordinate + same config ⇒ same height.
 *
 * Frequency is **cycles per world unit**. A wavelength of 32 (frequency 1/32)
 * means one large hill every 32 tiles/pixels — this is what makes coherent
 * terrain instead of white noise. `scale` is an alias for frequency.
 *
 * Pipeline:
 *   1. 2-octave Perlin continent (low frequency) — this is the land shape
 *   2. low-frequency domain warp of that field (bays / peninsulas)
 *   3. optional multiplicative island falloff (kills map corners, not the shape)
 *   4. smoothstep → solid coastline; hills / ridges only inland
 */
class TerrainSampler {
public:
    TerrainSampler() = default;

    float sample(float x, float y) const;
    /** @brief Height at the center of map tile (tileX, tileY). */
    float sampleTile(int tileX, int tileY) const;
    std::function<float(float, float)> asFunction() const;

    void      setSeed(uint32_t seed);
    uint32_t  getSeed() const;
    /** @brief Cycles per world unit (alias of frequency). Default 1/32. */
    void      setScale(float scale);
    float     getScale() const;
    void      setFrequency(float frequency);
    float     getFrequency() const;
    /** @brief Distance per large oscillation. Sets frequency = 1/wavelength. */
    void      setWavelength(float wavelength);
    float     getWavelength() const;
    void      setOctaves(int octaves);
    int       getOctaves() const;
    void      setLacunarity(float lacunarity);
    float     getLacunarity() const;
    void      setGain(float gain);
    float     getGain() const;
    void      setRidge(float ridge);
    float     getRidge() const;
    void      setWarp(float warp);
    float     getWarp() const;
    void      setExponent(float exponent);
    float     getExponent() const;
    void      setContinent(float continent);
    float     getContinent() const;
    void      setIsland(float island);
    float     getIsland() const;
    /** @brief Shore width of the land mask smoothstep. Smaller = cleaner, harder coast. */
    void      setCoastSoftness(float softness);
    float     getCoastSoftness() const;
    void      setWorldSize(int width, int height);
    int       getWorldWidth() const;
    int       getWorldHeight() const;
    void      setBase(float base);
    float     getBase() const;
    void      setAmplitude(float amplitude);
    float     getAmplitude() const;
    void      setClamp(bool enabled, float minHeight, float maxHeight);
    bool      isClamped() const;
    float     getClampMin() const;
    float     getClampMax() const;

    static TerrainSampler fromParams(const Params &params);

private:
    NoiseField field_;
    float      frequency_  = 1.f / 32.f;
    int        octaves_    = 5;
    float      lacunarity_ = 2.f;
    float      gain_       = 0.5f;
    float      ridge_      = 0.35f;
    float      warp_       = 0.35f;
    float      exponent_   = 2.f;
    float      continent_  = 0.55f;
    float      island_     = 0.38f;
    float      coastSoft_  = 0.12f;
    int        worldW_     = 0;
    int        worldH_     = 0;
    float      base_       = 0.f;
    float      amplitude_  = 1.f;
    bool       clamp_      = true;
    float      clampMin_   = 0.f;
    float      clampMax_   = 1.f;
};

struct TerrainBands {
    float waterMax = 0.25f;
    float sandMax  = 0.35f;
    float grassMax = 0.65f;
    float dirtMax  = 0.80f;
    float stoneMax = 0.92f;

    uint32_t semanticAt(float height) const;
    static TerrainBands fromParams(const Params &params);
};

}  // namespace eve::procgen
