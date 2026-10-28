#include "procgen/algorithms/CaveScallopHistory.h"

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

CaveScallopHistorySample sampleCaveScallopHistory(const CaveScallopHistoryInput& input) {
    CaveScallopHistorySample result;
    const CaveScallopSample  older = sampleCaveScallops(input.base);
    result.erosion                 = older.erosion;
    if (input.historyStrength <= 0.f) return result;

    const float phase       = float(input.base.seed % 997u) * (2.f * Pi / 997.f);
    const float stageWindow = 0.5f + 0.34f * std::sin(input.base.along * 4.3f + phase) +
                              0.16f * std::sin(input.base.angle * 2.f - input.base.along * 1.7f + phase * 0.53f);
    const float wallBand    = 0.38f + 0.62f * smoothstep(-0.5f, 0.82f, std::sin(input.base.angle + phase * 0.11f));
    result.youngerCoverage  = input.historyStrength * wallBand * smoothstep(0.18f, 0.78f, stageWindow);

    // Drainage capture and backflooding need not reverse an entire passage.
    // A long-wave mask changes direction only in coherent reaches.
    result.reversalMask = smoothstep(0.35f, 0.82f, 0.5f + 0.5f * std::sin(input.base.along * 2.6f - phase));
    result.secondaryScaleRatio =
        std::clamp(0.72f / std::sqrt(std::clamp(input.base.hydraulicIntensity, 0.35f, 1.45f)), 0.52f, 0.92f);

    CaveScallopInput youngerInput = input.base;
    youngerInput.baseScale *= result.secondaryScaleRatio;
    youngerInput.along = input.base.along * (1.f - 2.f * result.reversalMask);
    youngerInput.seed ^= 0x9e3779b9u;
    const CaveScallopSample younger = sampleCaveScallops(youngerInput);
    result.youngerErosion           = younger.erosion * result.youngerCoverage;

    // A younger stage can only remove more rock. Keeping its retreat bounded
    // preserves readable remnants of the older population between stage windows.
    result.erosion += result.youngerErosion * 0.36f;
    return result;
}

}  // namespace eve::procgen
