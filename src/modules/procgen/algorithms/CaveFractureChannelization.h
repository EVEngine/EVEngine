#pragma once

namespace eve::procgen {

/** @brief Inputs for aperture-driven reactive channelization within a cave fracture network. */
struct CaveFractureChannelizationInput {
    float primaryMask                 = 0.f;
    float secondaryMask               = 0.f;
    float primaryApertureMultiplier   = 1.f;
    float secondaryApertureMultiplier = 1.f;
    float hydraulicIntensity          = 1.f;
    float distanceFromInlet           = 0.f;
    float reactantPenetration         = 1.f;
};

/** @brief Observable local state of aperture-flow-dissolution feedback. */
struct CaveFractureChannelizationSample {
    float erosion                   = 0.f;
    float flowConcentration         = 0.f;
    float intersectionAmplification = 0.f;
    float reactantAccess            = 0.f;
};

/**
 * @brief Approximate fracture aperture-flow-dissolution feedback at one cave-wall sample.
 * @param input Local fracture apertures, masks, hydraulic intensity, and reactant penetration.
 * @return Local channelized erosion plus diagnostic feedback terms.
 */
[[nodiscard]] CaveFractureChannelizationSample sampleCaveFractureChannelization(
    const CaveFractureChannelizationInput& input);

}  // namespace eve::procgen
