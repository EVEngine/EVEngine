#include "procgen/algorithms/CaveWaterTable.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveWaterTableSample sampleCaveWaterTableCorrosion(const CaveWaterTableInput& input) {
    CaveWaterTableSample result;
    // passageAngle is zero at the crown and +/- pi/2 at the walls. Restricting
    // retreat to sidewalls prevents a horizontal density slice from cutting floors.
    result.sidewallMask       = std::pow(std::fabs(std::sin(input.passageAngle)), 1.45f);
    const float seedPhase     = float(input.seed % 1009u) * 0.006227141f;
    const float halfThickness = 0.022f + input.fluctuation * 0.035f;
    for (int stage = 0; stage < input.stages; ++stage) {
        const float nominalLevel = input.level - float(stage) * input.stageDrop;
        // Long-wavelength variation retains a recognizably level belt without making
        // it an artificial perfectly planar cut through every chamber.
        const float localLevel =
            nominalLevel + std::sin(input.along * 1.7f + seedPhase + float(stage) * 1.91f) * halfThickness * 0.22f;
        const float verticalDistance = std::fabs(input.y - localLevel);
        const float band             = 1.f - smoothstep(halfThickness, halfThickness * 2.6f, verticalDistance);
        const float preservation     = std::pow(0.78f, float(stage));
        const float erosion          = band * preservation * result.sidewallMask;
        if (erosion > result.erosion) {
            result.erosion       = erosion;
            result.dominantStage = stage;
            result.dominantLevel = nominalLevel;
        }
    }
    return result;
}

}  // namespace eve::procgen
