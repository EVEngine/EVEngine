#include "procgen/algorithms/CaveRoughnessTransfer.h"

#include <algorithm>

namespace eve::procgen {

CaveRoughnessTransferSample sampleCaveRoughnessTransfer(const CaveRoughnessTransferInput& input) {
    const float relief   = std::clamp(input.relief, -1.f, 1.f);
    const float flow     = std::clamp(input.hydraulicIntensity, 0.f, 2.f) * 0.5f;
    const float coupling = std::clamp(input.coupling, 0.f, 1.f);

    // Convex relief stays in contact with replenished water, while negative relief
    // represents recirculating or stagnant pockets. Strong flow partly ventilates the
    // pockets but increases transfer over exposed asperities. This is deliberately a
    // bounded sub-grid proxy; resolved flow remains owned by CaveHydrology.
    const float ridgeExposure = std::max(relief, 0.f) * (0.45f + 0.55f * flow);
    const float recessShelter = std::max(-relief, 0.f) * (1.f - 0.35f * flow);
    const float multiplier = std::clamp(1.f + coupling * (0.45f * ridgeExposure - 0.40f * recessShelter), 0.60f, 1.45f);
    return {multiplier, ridgeExposure, recessShelter};
}

}  // namespace eve::procgen
