#pragma once

#include <cstdint>

namespace eve::procgen {

struct CaveBiogenicCorrosionInput {
    float    along             = 0.f;
    float    angle             = 0.f;
    float    distance          = 0.f;
    float    radius            = 0.16f;
    float    hydraulicExposure = 1.f;
    float    strength          = 0.f;
    uint32_t seed              = 0;
};

struct CaveBiogenicCorrosionSample {
    float erosion                 = 0.f;
    float fluvialScallopRetention = 1.f;
};

/**
 * @brief Sample an airflow-aligned, guano-ammonia biogenic corrosion overprint.
 * @param input Passage-local coordinates, wet-film exposure, intensity, and deterministic seed.
 * @return Secondary erosion and the retained fraction of older fluvial scallops.
 */
CaveBiogenicCorrosionSample sampleCaveBiogenicCorrosion(const CaveBiogenicCorrosionInput& input);

}  // namespace eve::procgen
