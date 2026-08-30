#include "procgen/algorithms/CaveFractures.h"

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

CaveFractureSample sampleCaveFracture(const CaveFractureInput& input) {
    const float planeDistance =
        std::fabs(input.x * input.fracture.normalX + input.z * input.fracture.normalZ - input.fracture.offset);
    const float tangent   = -input.fracture.normalZ * input.x + input.fracture.normalX * input.z;
    const float seedPhase = float(input.seed % 1009u) * (2.f * Pi / 1009.f);

    // Natural fracture apertures are correlated patches, not independent voxel noise.
    const float apertureField      = 0.62f * std::sin(tangent * 7.2f + input.y * 2.1f + seedPhase) +
                                     0.38f * std::sin(tangent * 3.1f - input.y * 5.4f - seedPhase * 0.73f);
    const float apertureMultiplier = std::exp(input.apertureVariability * 0.55f * apertureField);
    const float aperture           = input.fracture.aperture * apertureMultiplier;
    const float planeMask          = 1.f - smoothstep(aperture, aperture * 5.f, planeDistance);

    // Jiang et al. (2025) observed stress-split dissolution fronts and persistent branches.
    // Two travelling centres divide, curve, and reconnect along the fracture plane.
    const float progression = input.y * 4.1f + seedPhase;
    const float split =
        (0.055f + 0.035f * std::sin(progression * 0.47f)) * smoothstep(-0.35f, 0.30f, std::sin(progression));
    const float drift          = 0.11f * std::sin(input.y * 2.6f + seedPhase * 1.37f);
    const float branchWidth    = 0.055f + 0.018f * (0.5f + 0.5f * std::sin(progression * 1.31f));
    const float branchA        = 1.f - smoothstep(branchWidth, branchWidth * 2.8f, std::fabs(tangent - drift - split));
    const float branchB        = 1.f - smoothstep(branchWidth, branchWidth * 2.8f, std::fabs(tangent - drift + split));
    const float branchCore     = std::max(branchA, branchB);
    const float branchOpenness = (1.f - input.stressControl) + input.stressControl * (0.18f + 0.82f * branchCore);

    return {planeMask * branchOpenness, apertureMultiplier, branchOpenness};
}

}  // namespace eve::procgen
