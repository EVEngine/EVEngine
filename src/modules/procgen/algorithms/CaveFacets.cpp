#include "procgen/algorithms/CaveFacets.h"

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
    uint32_t value = seed ^ 0x9e3779b9u;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    return float(value & 0xffffu) * (2.f * Pi / 65536.f);
}

}  // namespace

CaveFacetSample sampleCaveCondensationFacets(const CaveFacetInput& input) {
    CaveFacetSample result;
    if (input.strength <= 0.f || input.radius <= 0.f) return result;

    const float strength   = std::clamp(input.strength, 0.f, 1.f);
    result.facetCount      = 5 + int(input.seed % 3u);
    const float sector     = 2.f * Pi / float(result.facetCount);
    const float phase      = seedPhase(input.seed);
    const float localAngle = std::remainder(input.angle + phase, sector);
    const float wallMask   = 1.f - smoothstep(input.radius * 1.15f, input.radius * 2.1f, input.distance);

    // Slowly varying condensation/evaporation cells localize the otherwise planar
    // corrosion envelope. Side walls remain active while ceilings receive a modest gain.
    const float humidity    = smoothstep(0.18f, 0.82f, 0.5f + 0.5f * std::sin(input.along * 5.3f + phase));
    const float orientation = 0.68f + 0.32f * std::max(0.f, std::cos(input.angle));
    result.planarWeight     = strength * wallMask * humidity * orientation;
    if (result.planarWeight <= 0.f) return result;

    const float apothem       = input.radius + 0.014f * strength;
    const float polygonRadius = apothem / std::max(0.78f, std::cos(localAngle));
    result.retreat            = std::min(0.055f, std::max(0.f, polygonRadius - input.radius) * result.planarWeight);
    return result;
}

}  // namespace eve::procgen
