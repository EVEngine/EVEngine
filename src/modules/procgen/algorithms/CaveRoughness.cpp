#include "procgen/algorithms/CaveRoughness.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

uint32_t mixHash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float smooth(float value) { return value * value * (3.f - 2.f * value); }

float cornerNoise(int x, int y, int z, uint32_t seed) {
    uint32_t value = seed;
    value ^= mixHash(uint32_t(x) + 0x9e3779b9u);
    value ^= mixHash(uint32_t(y) + 0x85ebca6bu);
    value ^= mixHash(uint32_t(z) + 0xc2b2ae35u);
    return float(mixHash(value) & 0x00ffffffu) / float(0x007fffffu) - 1.f;
}

float valueNoise(float x, float y, float z, uint32_t seed) {
    const int   x0   = int(std::floor(x));
    const int   y0   = int(std::floor(y));
    const int   z0   = int(std::floor(z));
    const float tx   = smooth(x - float(x0));
    const float ty   = smooth(y - float(y0));
    const float tz   = smooth(z - float(z0));
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00  = lerp(cornerNoise(x0, y0, z0, seed), cornerNoise(x0 + 1, y0, z0, seed), tx);
    const float x10  = lerp(cornerNoise(x0, y0 + 1, z0, seed), cornerNoise(x0 + 1, y0 + 1, z0, seed), tx);
    const float x01  = lerp(cornerNoise(x0, y0, z0 + 1, seed), cornerNoise(x0 + 1, y0, z0 + 1, seed), tx);
    const float x11  = lerp(cornerNoise(x0, y0 + 1, z0 + 1, seed), cornerNoise(x0 + 1, y0 + 1, z0 + 1, seed), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

}  // namespace

CaveRoughnessSample sampleCaveWallRoughness(const CaveRoughnessInput& input) {
    // Frequency bands remain below the default voxel Nyquist limit. Vertical frequencies
    // are lower because bedding makes carbonate relief statistically anisotropic.
    const float macro  = valueNoise(input.x * 3.7f, input.y * 2.1f, input.z * 3.7f, input.seed ^ 0x21f0aaadu);
    const float meso   = valueNoise(input.x * 8.3f, input.y * 4.8f, input.z * 8.3f, input.seed ^ 0x91e10da5u);
    const float fine   = valueNoise(input.x * 16.7f, input.y * 9.5f, input.z * 16.7f, input.seed ^ 0xd1b54a35u);
    const float relief = std::clamp((macro * 0.72f + meso * 0.24f + fine * 0.08f) / 1.04f, -1.f, 1.f);
    return {relief, macro, meso, fine};
}

}  // namespace eve::procgen
