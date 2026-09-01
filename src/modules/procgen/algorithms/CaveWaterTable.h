#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for a staged epiphreatic water-table corrosion belt. */
struct CaveWaterTableInput {
    float    y            = 0.f;
    float    passageAngle = 0.f;
    float    along        = 0.f;
    float    level        = 0.f;
    float    stageDrop    = 0.18f;
    float    fluctuation  = 0.35f;
    int      stages       = 1;
    uint32_t seed         = 0;
};

/** @brief Local staged water-table corrosion and the dominant historical level. */
struct CaveWaterTableSample {
    float erosion       = 0.f;
    float sidewallMask  = 0.f;
    int   dominantStage = -1;
    float dominantLevel = 0.f;
};

/**
 * @brief Sample laterally continuous corrosion belts left by one or more water-table stages.
 * @param input Passage-local wall angle, vertical position, staged levels, and seed.
 * @return Local normalized corrosion, sidewall exposure, and dominant stage metadata.
 */
[[nodiscard]] CaveWaterTableSample sampleCaveWaterTableCorrosion(const CaveWaterTableInput& input);

}  // namespace eve::procgen
