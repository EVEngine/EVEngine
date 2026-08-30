#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for sediment-laden flood abrasion of a cave passage. */
struct CaveAbrasionInput {
    float    along              = 0.f;
    float    angle              = 0.f;
    float    hydraulicIntensity = 1.f;
    float    bendStrength       = 0.f;
    float    outerBankAngle     = 0.f;
    float    sedimentLoad       = 0.5f;
    uint32_t seed               = 0;
};

/** @brief Near-bed abrasion, sediment cover, and coherent vortex-cell response. */
struct CaveAbrasionSample {
    float erosion          = 0.f;
    float floorMask        = 0.f;
    float toolAvailability = 0.f;
    float coverProtection  = 0.f;
    float vortexMask       = 0.f;
};

/**
 * @brief Sample mechanical erosion by sediment-laden floods near the passage bed.
 * @param input Passage-local position, flow exposure, curvature, sediment load, and seed.
 * @return Bounded abrasion response including the non-monotonic tools-and-cover effect.
 */
[[nodiscard]] CaveAbrasionSample sampleCaveFloodAbrasion(const CaveAbrasionInput& input);

}  // namespace eve::procgen
