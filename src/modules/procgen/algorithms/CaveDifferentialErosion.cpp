#include "procgen/algorithms/CaveDifferentialErosion.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.1415926535f;

float smoothstep(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float seedPhase(uint32_t seed) {
    uint32_t value = seed ^ 0xa511e9b3u;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    return float(value & 0xffffu) * (2.f * Pi / 65536.f);
}

}  // namespace

CaveDifferentialErosionSample sampleCaveDifferentialVeinErosion(const CaveDifferentialErosionInput& input) {
    CaveDifferentialErosionSample result;
    if (input.strength <= 0.f || input.radius <= 0.f) return result;

    const float strength = std::clamp(input.strength, 0.f, 1.f);
    const float phase    = seedPhase(input.seed);
    const float c        = std::cos(phase);
    const float s        = std::sin(phase);
    const float u        = input.x * c + input.z * s;
    const float v        = -input.x * s + input.z * c;

    // Two gently warped vein sets leave an intersecting boxwork. Their narrow
    // resistant cores stand proud only because the surrounding host rock retreats.
    const float veinA    = std::fabs(std::sin((u + input.y * 0.22f) * 3.4f * Pi + phase));
    const float veinB    = std::fabs(std::sin((v - input.y * 0.17f) * 2.7f * Pi - phase * 0.61f));
    const float network  = std::max(1.f - smoothstep(0.035f, 0.16f, veinA), 1.f - smoothstep(0.035f, 0.16f, veinB));
    const float patch    = smoothstep(0.2f, 0.78f,
                                      0.5f + 0.27f * std::sin(input.x * 2.1f + input.y * 1.3f + phase) +
                                          0.23f * std::sin(input.z * 2.6f - input.y * 0.8f - phase * 0.7f));
    const float wallMask = 1.f - smoothstep(input.radius * 1.1f, input.radius * 2.1f, input.distance);

    result.veinProtection   = strength * patch * wallMask * network;
    const float exposedHost = strength * patch * wallMask * (1.f - 0.94f * network);
    result.hostRetreat      = std::min(0.04f, 0.038f * exposedHost);
    return result;
}

}  // namespace eve::procgen
