#include "procgen/algorithms/CaveKarren.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveKarrenSample sampleCaveStreamKarren(const CaveKarrenInput& input) {
    CaveKarrenSample result;
    // Passage angle is zero at the roof and pi at the gravity-defined floor.
    result.floorExposure      = smoothstep(0.28f, 0.82f, -std::cos(input.passageAngle));
    const float primary       = std::pow(std::clamp(input.primaryFracture, 0.f, 1.f), 0.72f);
    const float secondary     = std::pow(std::clamp(input.secondaryFracture, 0.f, 1.f), 0.72f);
    result.intersectionPocket = primary * secondary;
    result.fractureGuidance =
        std::clamp(primary * 0.68f + secondary * 0.42f + result.intersectionPocket * 0.48f, 0.f, 1.f);
    const float activeFlow = smoothstep(0.32f, 1.25f, input.hydraulicIntensity);
    result.erosion         = result.floorExposure * result.fractureGuidance * activeFlow;
    return result;
}

}  // namespace eve::procgen
