#include "procgen/algorithms/CaveAbrasion.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveAbrasionSample sampleCaveFloodAbrasion(const CaveAbrasionInput& input) {
    CaveAbrasionSample result;
    if (input.sedimentLoad <= 0.f) return result;

    // angle is zero at the crown and pi at the floor. Suspended impacts and
    // saltating grains rapidly lose erosive power above the near-bed zone.
    result.floorMask          = std::pow(std::max(0.f, -std::cos(input.angle)), 1.65f);
    result.toolAvailability   = smoothstep(0.025f, 0.28f, input.sedimentLoad);
    result.coverProtection    = std::clamp(input.sedimentLoad * input.sedimentLoad * 0.68f, 0.f, 0.68f);
    const float toolsAndCover = result.toolAvailability * (1.f - result.coverProtection);

    const float seedPhase = float(input.seed % 2039u) * 0.003081503f;
    // Low-frequency coherent cells represent persistent recirculation and
    // impact patches. They are enhanced at bends but remain present in straight floods.
    const float cell        = 0.5f + 0.5f * std::sin(input.along * 12.5f + seedPhase +
                                                     std::sin(input.along * 3.1f + seedPhase * 1.7f) * 1.15f);
    const float lateralCell = 0.5f + 0.5f * std::cos((input.angle - 3.1415926535f) * 3.2f +
                                                     std::sin(input.along * 5.3f + seedPhase * 0.7f) * 0.8f);
    const float bendSide    = std::max(0.f, std::cos(input.angle - input.outerBankAngle));
    result.vortexMask = std::clamp(cell * lateralCell * (0.55f + input.bendStrength * bendSide * 0.75f), 0.f, 1.f);
    const float fluting =
        0.35f + 0.65f * smoothstep(-0.45f, 0.65f, std::sin(input.along * 25.f + input.angle * 2.f + seedPhase * 2.3f));
    const float streamPower = std::pow(std::clamp(input.hydraulicIntensity, 0.f, 1.7f), 1.25f);
    result.erosion          = std::clamp(
        result.floorMask * toolsAndCover * streamPower * (0.38f + 0.62f * result.vortexMask) * fluting, 0.f, 1.f);
    return result;
}

}  // namespace eve::procgen
