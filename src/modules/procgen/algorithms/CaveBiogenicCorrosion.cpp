#include "procgen/algorithms/CaveBiogenicCorrosion.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.1415926535f;

float smoothstep(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float seedPhase(uint32_t seed, uint32_t salt) {
    uint32_t value = seed ^ salt;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    return float(value & 0xffffu) * (2.f * Pi / 65536.f);
}

}  // namespace

CaveBiogenicCorrosionSample sampleCaveBiogenicCorrosion(const CaveBiogenicCorrosionInput& input) {
    CaveBiogenicCorrosionSample result;
    if (input.strength <= 0.f) return result;

    const float strength = std::clamp(input.strength, 0.f, 1.f);
    const float wallMask = 1.f - smoothstep(input.radius * 1.1f, input.radius * 2.1f, input.distance);
    // Fast water films remove ammonia and wall microbial communities. Slow or absent
    // films permit the subaerial overprint to erase older fluvial signatures.
    const float filmProtection     = smoothstep(0.95f, 1.45f, input.hydraulicExposure);
    const float exposure           = wallMask * (1.f - 0.82f * filmProtection);
    result.fluvialScallopRetention = 1.f - strength * exposure * 0.86f;

    const float largeScale  = std::max(0.22f, input.radius * 2.8f);
    const float phaseA      = seedPhase(input.seed, 0x81f3a62du);
    const float phaseB      = seedPhase(input.seed, 0x43b7d159u);
    const float alongWave   = 0.5f + 0.5f * std::cos(input.along * 2.f * Pi / largeScale + phaseA);
    const float broadLobe   = 0.5f + 0.5f * std::cos(input.angle * 3.f + phaseB + 0.25f * std::sin(input.along * 3.f));
    const float megascallop = std::pow(std::clamp(alongWave * broadLobe, 0.f, 1.f), 0.58f);

    // Long angular bands change only slowly along the passage, producing flutes
    // aligned with air transport instead of isotropic surface noise.
    const float fluteWave = std::max(0.f, std::cos(input.angle * 7.f + phaseA + 0.14f * std::sin(input.along * 5.f)));
    const float fluting   = std::pow(fluteWave, 1.7f);
    const float sponge    = std::max(0.f, std::sin(input.along * 11.f + phaseB) * std::cos(input.angle * 5.f - phaseA));
    result.erosion        = strength * exposure * (0.56f * megascallop + 0.29f * fluting + 0.15f * sponge);
    return result;
}

}  // namespace eve::procgen
