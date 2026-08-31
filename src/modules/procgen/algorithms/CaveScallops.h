#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct CaveScallopInput {
    float    along              = 0.f;
    float    angle              = 0.f;
    float    distance           = 0.f;
    float    radius             = 0.16f;
    float    hydraulicIntensity = 1.f;
    float    baseScale          = 0.12f;
    float    hydraulicScaling   = 0.f;
    float    maturity           = 0.f;
    float    bendUndercut       = 0.f;
    float    bendStrength       = 0.f;
    float    outerBankAngle     = 0.f;
    float    scaleVariability   = 0.f;
    float    flowSeparation     = 0.f;
    uint32_t seed               = 0;
};

struct CaveScallopSample {
    float erosion         = 0.f;
    float scale           = 0.12f;
    float scaleMultiplier = 1.f;
};

struct CavePassageFrame {
    float             along              = 0.f;
    float             angle              = 0.f;
    float             distance           = 1e9f;
    float             radius             = 0.16f;
    float             hydraulicIntensity = 1.f;
    float             bendStrength       = 0.f;
    float             outerBankAngle     = 0.f;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
};

CaveScallopSample sampleCaveScallops(const CaveScallopInput& input);
CavePassageFrame  nearestCavePassageFrame(const CaveHydrologyVec3& point, const std::vector<CaveHydrologyPoint>& path,
                                          const std::vector<float>& hydraulicIntensity);

}  // namespace eve::procgen
