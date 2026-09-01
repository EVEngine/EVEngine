#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for lithology-selective bedding and stylolite dissolution. */
struct CaveLithologyInput {
    float    x                  = 0.f;
    float    y                  = 0.f;
    float    z                  = 0.f;
    float    along              = 0.f;
    float    hydraulicIntensity = 1.f;
    float    heterogeneity      = 0.f;
    uint32_t seed               = 0;
};

/** @brief Local bed resistance, stylolite exposure, and selective retreat. */
struct CaveLithologySample {
    float bedResistance = 1.f;
    float styloliteMask = 0.f;
    float interbedMask  = 0.f;
    float retreat       = 0.f;
    int   bedIndex      = 0;
};

/**
 * @brief Sample flow-accessible dissolution caused by contrasting beds and clustered stylolites.
 * @param input Normalized cave position, passage flow exposure, heterogeneity, and seed.
 * @return Bounded selective retreat and observable lithology fields.
 */
[[nodiscard]] CaveLithologySample sampleCaveLithology(const CaveLithologyInput& input);

}  // namespace eve::procgen
