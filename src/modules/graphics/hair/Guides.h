#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eve::graphics::hair {

/**
 * @brief How render strands follow deformed guides (UE interpolation analogue).
 *
 * - Rigid: weighted guide root translation applied to the whole strand.
 * - Offset: per-point delta sampled along each guide at the same `u`.
 * - Smooth: Offset with softer distance weights (more guides influence).
 */
enum class InterpolationMode : uint8_t { Rigid = 0, Offset = 1, Smooth = 2 };

/** @brief One guide contributing to a render strand. */
struct GuideInfluence {
    uint32_t guideIndex = 0;
    float weight = 0.f;
};

/**
 * @brief Up to kMaxInfluences guides influencing one render strand.
 * @ownership Owned by the weights table returned from `buildGuideWeights`.
 */
struct StrandGuideWeights {
    static constexpr int kMaxInfluences = 4;
    GuideInfluence influencers[kMaxInfluences]{};
    int count = 0;
};

/**
 * @brief Pick a deterministic subset of curves as guides (density thinning).
 *
 * Keeps about `guideFraction` of curves (clamped to [1/curveCount, 1]),
 * strided from index 0. Useful when an asset has no imported guides.
 *
 * @ownership Returned datas uniquely owned by the caller.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<StrandsDatas> extractGuides(const StrandsDatas &strands, float guideFraction);

/**
 * @brief Build k-nearest guide weights for every render strand (by root distance).
 *
 * Weights are inverse-distance (Smooth uses a softer exponent). Each row sums
 * to 1 when at least one guide exists.
 *
 * @param maxInfluences Clamped to [1, StrandGuideWeights::kMaxInfluences].
 */
[[nodiscard]] Result<std::vector<StrandGuideWeights>>
EVENGINE_API_BACKENDS buildGuideWeights(const StrandsDatas &strands, const StrandsDatas &guides, int maxInfluences = 3,
                  InterpolationMode mode = InterpolationMode::Offset);

/**
 * @brief Drive rest render strands with deformed guides using precomputed weights.
 * @ownership Returned datas uniquely owned by the caller; inputs unchanged.
 */
[[nodiscard]] Result<StrandsDatas>
EVENGINE_API_BACKENDS interpolateStrands(const StrandsDatas &strandsRest, const StrandsDatas &guidesRest,
                   const StrandsDatas &guidesDeformed,
                   const std::vector<StrandGuideWeights> &weights, InterpolationMode mode);

}  // namespace eve::graphics::hair
