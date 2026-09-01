#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for deterministic, band-limited cave-wall roughness. */
struct CaveRoughnessInput {
    float    x    = 0.f;
    float    y    = 0.f;
    float    z    = 0.f;
    uint32_t seed = 0;
};

/** @brief Multi-scale wall relief and its separated spatial-frequency bands. */
struct CaveRoughnessSample {
    float relief    = 0.f;
    float macroBand = 0.f;
    float mesoBand  = 0.f;
    float fineBand  = 0.f;
};

/**
 * @brief Sample a three-band, spatially correlated cave-wall relief field.
 * @param input Normalized position and deterministic seed.
 * @return Bounded combined relief and individual frequency bands.
 */
[[nodiscard]] CaveRoughnessSample sampleCaveWallRoughness(const CaveRoughnessInput& input);

}  // namespace eve::procgen
