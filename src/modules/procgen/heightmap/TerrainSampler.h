#pragma once
#include "common/Export.h"


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
class EVENGINE_API_DOMAINS TerrainSampler {
public:
    /** @brief Terrain sampler. */
    TerrainSampler() = default;

    /** @brief Sample. */
    float sample(float x, float y) const;
    /** @brief Height at the center of map tile (tileX, tileY). */
    float sampleTile(int tileX, int tileY) const;
    /** @brief Float. */
    std::function<float(float, float)> asFunction() const;

    /** @brief Sets the seed. */
    void      setSeed(uint32_t seed);
    /** @brief Returns the seed. */
    uint32_t  getSeed() const;
    /** @brief Cycles per world unit (alias of frequency). Default 1/32. */
    void      setScale(float scale);
    /** @brief Returns the scale. */
    float     getScale() const;
    /** @brief Sets the frequency. */
    void      setFrequency(float frequency);
    /** @brief Returns the frequency. */
    float     getFrequency() const;
    /** @brief Distance per large oscillation. Sets frequency = 1/wavelength. */
    void      setWavelength(float wavelength);
    /** @brief Returns the wavelength. */
    float     getWavelength() const;
    /** @brief Sets the octaves. */
    void      setOctaves(int octaves);
    /** @brief Returns the octaves. */
    int       getOctaves() const;
    /** @brief Sets the lacunarity. */
    void      setLacunarity(float lacunarity);
    /** @brief Returns the lacunarity. */
    float     getLacunarity() const;
    /** @brief Sets the gain. */
    void      setGain(float gain);
    /** @brief Returns the gain. */
    float     getGain() const;
    /** @brief Sets the ridge. */
    void      setRidge(float ridge);
    /** @brief Returns the ridge. */
    float     getRidge() const;
    /** @brief Sets the warp. */
    void      setWarp(float warp);
    /** @brief Returns the warp. */
    float     getWarp() const;
    /** @brief Sets the exponent. */
    void      setExponent(float exponent);
    /** @brief Returns the exponent. */
    float     getExponent() const;
    /** @brief Sets the continent. */
    void      setContinent(float continent);
    /** @brief Returns the continent. */
    float     getContinent() const;
    /** @brief Sets the island. */
    void      setIsland(float island);
    /** @brief Returns the island. */
    float     getIsland() const;
    /** @brief Shore width of the land mask smoothstep. Smaller = cleaner, harder coast. */
    void      setCoastSoftness(float softness);
    /** @brief Returns the coast softness. */
    float     getCoastSoftness() const;
    /** @brief Sets the world size. */
    void      setWorldSize(int width, int height);
    /** @brief Returns the world width. */
    int       getWorldWidth() const;
    /** @brief Returns the world height. */
    int       getWorldHeight() const;
    /** @brief Sets the base. */
    void      setBase(float base);
    /** @brief Returns the base. */
    float     getBase() const;
    /** @brief Sets the amplitude. */
    void      setAmplitude(float amplitude);
    /** @brief Returns the amplitude. */
    float     getAmplitude() const;
    /** @brief Sets the clamp. */
    void      setClamp(bool enabled, float minHeight, float maxHeight);
    /** @brief True when clamped. */
    bool      isClamped() const;
    /** @brief Returns the clamp min. */
    float     getClampMin() const;
    /** @brief Returns the clamp max. */
    float     getClampMax() const;

    /** @brief From params. */
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

/** @brief TerrainBands public API. */
struct TerrainBands {
    float waterMax = 0.25f;
    float sandMax  = 0.35f;
    float grassMax = 0.65f;
    float dirtMax  = 0.80f;
    float stoneMax = 0.92f;

    /** @brief Semantic at. */
    uint32_t semanticAt(float height) const;
    /** @brief From params. */
    static TerrainBands fromParams(const Params &params);
};

}  // namespace eve::procgen
