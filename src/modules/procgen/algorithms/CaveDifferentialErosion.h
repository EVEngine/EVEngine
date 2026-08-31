#pragma once

#include <cstdint>

namespace eve::procgen {

struct CaveDifferentialErosionInput {
    float    x        = 0.f;
    float    y        = 0.f;
    float    z        = 0.f;
    float    distance = 0.f;
    float    radius   = 0.16f;
    float    strength = 0.f;
    uint32_t seed     = 0;
};

struct CaveDifferentialErosionSample {
    float hostRetreat    = 0.f;
    float veinProtection = 0.f;
};

/**
 * @brief Sample host-rock retreat around resistant intersecting mineral veins.
 * @param input World position, local passage shell, normalized intensity, and deterministic seed.
 * @return Bounded host retreat and the local resistant-vein protection weight.
 */
CaveDifferentialErosionSample sampleCaveDifferentialVeinErosion(const CaveDifferentialErosionInput& input);

}  // namespace eve::procgen
