#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief One deterministic structural fracture plane in normalized cave space. */
struct CaveFracture {
    float normalX  = 1.f;
    float normalZ  = 0.f;
    float offset   = 0.f;
    float aperture = 0.02f;
};

/** @brief Inputs for heterogeneous, stress-controlled fracture dissolution. */
struct CaveFractureInput {
    float        x = 0.f;
    float        y = 0.f;
    float        z = 0.f;
    CaveFracture fracture;
    float        apertureVariability = 0.f;
    float        stressControl       = 0.f;
    uint32_t     seed                = 0;
};

/** @brief Observable local state of a sampled dissolving fracture. */
struct CaveFractureSample {
    float mask               = 0.f;
    float apertureMultiplier = 1.f;
    float branchOpenness     = 1.f;
};

/**
 * @brief Sample a spatially correlated fracture aperture and stress-split dissolution front.
 * @param input Point, fracture geometry, opt-in controls, and deterministic seed.
 * @return Local dissolution mask plus diagnostic aperture and branch factors.
 */
[[nodiscard]] CaveFractureSample sampleCaveFracture(const CaveFractureInput& input);

}  // namespace eve::procgen
