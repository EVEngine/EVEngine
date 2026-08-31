#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for flood plucking of fracture-bounded cave-floor blocks. */
struct CavePluckingInput {
    float    x                     = 0.f;
    float    y                     = 0.f;
    float    z                     = 0.f;
    float    angle                 = 0.f;
    float    hydraulicIntensity    = 1.f;
    float    primaryFractureMask   = 0.f;
    float    secondaryFractureMask = 0.f;
    float    blockScale            = 0.1f;
    uint32_t seed                  = 0;
};

/** @brief Local fracture predisposition, hydraulic threshold, and block removal response. */
struct CavePluckingSample {
    float erosion                = 0.f;
    float fracturePredisposition = 0.f;
    float hydraulicActivation    = 0.f;
    float blockMask              = 0.f;
};

/**
 * @brief Sample thresholded removal of blocks bounded by one or more exposed fractures.
 * @param input Cave position, near-bed angle, flow, fracture masks, block scale, and seed.
 * @return Bounded blocky retreat response and its controlling factors.
 */
[[nodiscard]] CavePluckingSample sampleCaveFloodPlucking(const CavePluckingInput& input);

}  // namespace eve::procgen
