#include "procgen/algorithms/CaveMineralArmoring.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveMineralArmoringSample sampleCaveMineralArmoring(const CaveMineralArmoringInput& input) {
    CaveMineralArmoringSample result;
    if (input.mineralSupply <= 0.f) return result;

    const float phase = float(input.seed % 1013u) * (2.f * Pi / 1013.f);
    const float patch = 0.5f + 0.3f * std::sin(input.passageAlong * 8.7f + phase) +
                        0.2f * std::sin(input.passageAngle * 3.f - input.passageAlong * 3.9f + phase * 0.61f);
    result.coatingCoverage =
        std::clamp(input.mineralSupply * (0.28f + 0.72f * smoothstep(0.18f, 0.82f, patch)), 0.f, 1.f);

    // Fast wall flow inhibits accumulation and strips part of an existing rind;
    // sheltered wall sectors retain a coherent secondary-mineral shield.
    const float hydraulicRemoval = smoothstep(0.72f, 1.38f, input.hydraulicIntensity);
    result.hydraulicRetention    = result.coatingCoverage * (1.f - 0.78f * hydraulicRemoval);

    // Dense coatings reduced CaCO3 dissolution by roughly one order of magnitude
    // in the 2025 limestone microfluidic experiments.  The exponential mapping
    // reaches that bound only for a fully retained coating.
    result.dissolutionRetention = std::exp(-2.302585093f * result.hydraulicRetention);
    return result;
}

}  // namespace eve::procgen
