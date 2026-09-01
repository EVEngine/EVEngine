#pragma once

#include <cstdint>

namespace eve::procgen {

/** @brief Inputs for secondary-mineral shielding of a dissolving cave wall. */
struct CaveMineralArmoringInput {
    float    passageAlong       = 0.f;
    float    passageAngle       = 0.f;
    float    hydraulicIntensity = 1.f;
    float    mineralSupply      = 0.f;
    uint32_t seed               = 0;
};

/** @brief Observable local state of a retained secondary-mineral coating. */
struct CaveMineralArmoringSample {
    float coatingCoverage      = 0.f;
    float hydraulicRetention   = 0.f;
    float dissolutionRetention = 1.f;
};

/**
 * @brief Sample patchy secondary-mineral coating and its protection of carbonate wall rock.
 * @param input Wall coordinates, hydraulic removal, mineral supply, and deterministic seed.
 * @return Coating coverage, retained fraction, and remaining dissolution-rate multiplier.
 */
[[nodiscard]] CaveMineralArmoringSample sampleCaveMineralArmoring(const CaveMineralArmoringInput& input);

}  // namespace eve::procgen
