#pragma once

namespace eve::procgen {

/** @brief Inputs for rough-wall control of local dissolution mass transfer. */
struct CaveRoughnessTransferInput {
    float relief             = 0.f;
    float hydraulicIntensity = 1.f;
    float coupling           = 0.f;
};

/** @brief Bounded mass-transfer response of an exposed ridge or sheltered recess. */
struct CaveRoughnessTransferSample {
    float massTransferMultiplier = 1.f;
    float ridgeExposure          = 0.f;
    float recessShelter          = 0.f;
};

/**
 * @brief Estimate local reactive transport over a rough carbonate wall.
 * @param input Signed wall relief, hydraulic exposure, and opt-in coupling strength.
 * @return Deterministic ridge exposure, recess shelter, and bounded rate multiplier.
 */
[[nodiscard]] CaveRoughnessTransferSample sampleCaveRoughnessTransfer(const CaveRoughnessTransferInput& input);

}  // namespace eve::procgen
