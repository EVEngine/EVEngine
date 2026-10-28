#include "procgen/algorithms/CaveMicrostructure.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

uint32_t microHash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float noise(float x, float y, float z, uint32_t seed) {
    const int   ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    const float fx = x - float(ix), fy = y - float(iy), fz = z - float(iz);
    auto        smooth = [](float t) { return t * t * (3.f - 2.f * t); };
    auto        sample = [seed](int sx, int sy, int sz) {
        const uint32_t h =
            microHash(uint32_t(sx) * 73856093u ^ uint32_t(sy) * 19349663u ^ uint32_t(sz) * 83492791u ^ seed);
        return float(h & 0xffffu) / 32767.5f - 1.f;
    };
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float ux = smooth(fx), uy = smooth(fy), uz = smooth(fz);
    const float x00 = lerp(sample(ix, iy, iz), sample(ix + 1, iy, iz), ux);
    const float x10 = lerp(sample(ix, iy + 1, iz), sample(ix + 1, iy + 1, iz), ux);
    const float x01 = lerp(sample(ix, iy, iz + 1), sample(ix + 1, iy, iz + 1), ux);
    const float x11 = lerp(sample(ix, iy + 1, iz + 1), sample(ix + 1, iy + 1, iz + 1), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveMicrostructureSample sampleCaveMicrostructure(float x, float y, float z, uint32_t seed, float heterogeneity,
                                                  float microporosityAccess, float permeabilityContrast) {
    if (heterogeneity <= 0.f) return {};

    // A coarse connected field represents intergranular macropores; a finer field
    // represents intragranular surface that can be reached without becoming a conduit.
    const float coarse            = 0.68f * noise(x * 3.1f, y * 2.2f, z * 3.1f, seed ^ 0x93c467e3u) +
                                    0.32f * noise(x * 7.3f, y * 4.6f, z * 7.3f, seed ^ 0x6d2b79f5u);
    const float fine              = noise(x * 13.7f, y * 9.1f, z * 13.7f, seed ^ 0xa511e9b3u);
    const float connectedPore     = smoothstep(-0.42f, 0.58f, coarse);
    const float accessibleSurface = smoothstep(-0.55f, 0.48f, 0.65f * fine + 0.35f * coarse);

    const float localized           = 0.22f + 1.58f * std::pow(connectedPore, 1.f + 5.f * permeabilityContrast);
    const float distributed         = 0.72f + 0.48f * accessibleSurface;
    const float reactionPattern     = microporosityAccess * distributed + (1.f - microporosityAccess) * localized;
    const float permeabilityPattern = 0.42f + 1.28f * std::pow(connectedPore, 1.f + 3.f * permeabilityContrast);

    CaveMicrostructureSample result;
    result.reactiveSurface = std::clamp(1.f + heterogeneity * (reactionPattern - 1.f), 0.2f, 1.8f);
    result.permeability =
        std::clamp(1.f + heterogeneity * permeabilityContrast * (permeabilityPattern - 1.f), 0.35f, 1.7f);
    return result;
}

}  // namespace eve::procgen
