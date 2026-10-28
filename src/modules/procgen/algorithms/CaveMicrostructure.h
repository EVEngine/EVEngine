#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief CaveMicrostructureSample public API. */
struct CaveMicrostructureSample {
    float reactiveSurface = 1.f;
    float permeability    = 1.f;
};

/** @brief Sample cave microstructure. */
CaveMicrostructureSample sampleCaveMicrostructure(float x, float y, float z, uint32_t seed, float heterogeneity,
                                                  float microporosityAccess, float permeabilityContrast);

}  // namespace eve::procgen
