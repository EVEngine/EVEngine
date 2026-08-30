#include "procgen/algorithms/CaveFractureChannelization.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveFractureChannelizationSample sampleCaveFractureChannelization(const CaveFractureChannelizationInput& input) {
    CaveFractureChannelizationSample result;
    if (input.primaryMask <= 0.f || input.primaryApertureMultiplier <= 1.f) return result;

    // Parallel-plate fracture conductance scales with aperture cubed.  Expressing
    // only the excess above the uniform aperture preserves the legacy field when
    // aperture variability is disabled.
    const float conductance  = std::pow(std::max(input.primaryApertureMultiplier, 0.f), 3.f);
    result.flowConcentration = smoothstep(1.f, 4.6f, conductance);

    // A second open fracture supplies another advective face and creates the
    // intersection-centred windows predicted by reactive fracture-network models.
    const float secondaryConductance = std::pow(std::max(input.secondaryApertureMultiplier, 0.f), 3.f);
    result.intersectionAmplification = std::sqrt(std::clamp(input.primaryMask * input.secondaryMask, 0.f, 1.f)) *
                                       smoothstep(0.9f, 3.8f, secondaryConductance);

    const float penetration      = std::max(input.reactantPenetration, 0.02f);
    const float downstreamSupply = 0.25f + 0.75f * std::exp(-std::max(input.distanceFromInlet, 0.f) / penetration);
    result.reactantAccess        = std::clamp(downstreamSupply * (0.45f + 0.55f * input.hydraulicIntensity), 0.f, 1.4f);
    result.erosion               = input.primaryMask * result.flowConcentration * result.reactantAccess *
                                   (0.58f + 0.42f * result.intersectionAmplification);
    return result;
}

}  // namespace eve::procgen
