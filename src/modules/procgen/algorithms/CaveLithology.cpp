#include "procgen/algorithms/CaveLithology.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

uint32_t hash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float unitHash(uint32_t value) { return float(hash(value) & 0xffffu) / 65535.f; }

uint32_t signedBedKey(int bedIndex) { return bedIndex >= 0 ? uint32_t(bedIndex) * 2u : uint32_t(-bedIndex) * 2u - 1u; }

}  // namespace

CaveLithologySample sampleCaveLithology(const CaveLithologyInput& input) {
    CaveLithologySample result;
    if (input.heterogeneity <= 0.f) return result;

    const float seedPhase = float(input.seed % 4093u) * 0.001534326f;
    // A long-wavelength warp keeps contacts stratigraphically coherent without
    // turning them into perfectly planar, synthetic slices.
    const float warp               = std::sin(input.x * 2.1f + input.z * 1.7f + seedPhase) * 0.035f +
                                     std::sin(input.x * 4.7f - input.z * 3.2f + seedPhase * 1.61f) * 0.012f;
    const float bedCoordinate      = (input.y + warp) * 7.25f;
    result.bedIndex                = int(std::floor(bedCoordinate + 0.5f));
    const float    contactDistance = std::fabs(bedCoordinate - float(result.bedIndex));
    const uint32_t bedKey          = signedBedKey(result.bedIndex) ^ input.seed;

    // Intrinsic dissolution contrast is correlated by bed, not white noise per voxel.
    const float bedReactivity = unitHash(bedKey ^ 0x9e3779b9u);
    result.bedResistance      = 1.35f - bedReactivity * 0.70f * input.heterogeneity;
    const float weakBed       = smoothstep(0.44f, 0.78f, bedReactivity);
    const float bedInterior =
        smoothstep(0.05f, 0.36f, contactDistance) * (1.f - smoothstep(0.36f, 0.49f, contactDistance));
    result.interbedMask = weakBed * bedInterior;

    // Only some contacts carry stylolite clusters. Lateral modulation produces
    // connected patches rather than an implausible groove around every wall.
    const float stylolitePropensity = unitHash(bedKey ^ 0x85ebca6bu);
    const float activeContact       = smoothstep(0.48f, 0.68f, stylolitePropensity);
    const float thinContact         = 1.f - smoothstep(0.035f, 0.16f, contactDistance);
    const float lateralCluster      = smoothstep(
        -0.28f, 0.35f, std::sin(input.along * 2.4f + input.z * 3.1f + seedPhase + float(result.bedIndex) * 1.37f));
    result.styloliteMask = activeContact * thinContact * lateralCluster;

    // Preferentially flushed contacts receive fresh undersaturated water, while
    // stagnant beds retain a weak background response.
    const float hydraulicAccess = std::clamp(0.18f + 0.82f * input.hydraulicIntensity, 0.18f, 1.35f);
    const float selectiveDissolution =
        result.styloliteMask * 0.72f + result.interbedMask * (1.15f - result.bedResistance) * 0.65f;
    result.retreat = std::clamp(selectiveDissolution * hydraulicAccess * input.heterogeneity, 0.f, 1.f);
    return result;
}

}  // namespace eve::procgen
