#pragma once

namespace eve::procgen {

/** @brief Inputs for fracture-guided dissolution grooves on an exposed cave-stream bed. */
struct CaveKarrenInput {
    float passageAngle       = 0.f;
    float hydraulicIntensity = 1.f;
    float primaryFracture    = 0.f;
    float secondaryFracture  = 0.f;
};

/** @brief Observable stream-bed karren response at one cave-space sample. */
struct CaveKarrenSample {
    float erosion            = 0.f;
    float floorExposure      = 0.f;
    float fractureGuidance   = 0.f;
    float intersectionPocket = 0.f;
};

/**
 * @brief Sample stream-bed solution grooves guided by the two strongest local fracture sets.
 * @param input Passage-frame orientation, authoritative hydraulic exposure, and fracture masks.
 * @return Bounded erosion response that is inactive away from the passage floor.
 */
[[nodiscard]] CaveKarrenSample sampleCaveStreamKarren(const CaveKarrenInput& input);

}  // namespace eve::procgen
