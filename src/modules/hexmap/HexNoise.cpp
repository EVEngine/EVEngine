#include "hexmap/HexNoise.h"

#include <cmath>

namespace eve::hexmap {
namespace {

/** @brief Integer hash with good avalanche on the low bits (xxhash-style mix). */
[[nodiscard]] std::uint32_t hash3(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    std::uint32_t h = x * 0x9E3779B1u;
    h ^= y * 0x85EBCA77u;
    h = (h ^ (h >> 15)) * 0xC2B2AE3Du;
    h ^= z * 0x27D4EB2Fu;
    h = (h ^ (h >> 13)) * 0x165667B1u;
    return h ^ (h >> 16);
}

[[nodiscard]] float toUnit(std::uint32_t value) noexcept {
    // 24 significant bits -> [0, 1)
    return static_cast<float>(value >> 8) * (1.f / 16777216.f);
}

[[nodiscard]] float smooth(float t) noexcept { return t * t * (3.f - 2.f * t); }

}  // namespace

float HexNoise::lattice(int ix, int iz, int channel) const noexcept {
    return toUnit(hash3(static_cast<std::uint32_t>(ix), static_cast<std::uint32_t>(iz),
                        seed_ + static_cast<std::uint32_t>(channel) * 0x9E3779B9u));
}

HexVec4 HexNoise::sample(float worldX, float worldZ) const noexcept {
    const float fx = worldX * (1.f / kCellScale);
    const float fz = worldZ * (1.f / kCellScale);
    const float bx = std::floor(fx);
    const float bz = std::floor(fz);
    const int   ix = static_cast<int>(bx);
    const int   iz = static_cast<int>(bz);
    const float tx = smooth(fx - bx);
    const float tz = smooth(fz - bz);

    HexVec4 result;
    float*  channels[4] = {&result.x, &result.y, &result.z, &result.w};
    for (int channel = 0; channel < 4; ++channel) {
        const float c00    = lattice(ix, iz, channel);
        const float c10    = lattice(ix + 1, iz, channel);
        const float c01    = lattice(ix, iz + 1, channel);
        const float c11    = lattice(ix + 1, iz + 1, channel);
        const float top    = c00 + (c10 - c00) * tx;
        const float bottom = c01 + (c11 - c01) * tx;
        *channels[channel] = top + (bottom - top) * tz;
    }
    return result;
}

HexVec3 HexNoise::perturb(HexVec3 position) const noexcept {
    const HexVec4 n = sample(position.x, position.z);
    position.x += (n.x * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    position.z += (n.z * 2.f - 1.f) * HexMetrics::kCellPerturbStrength;
    return position;
}

float HexNoise::elevationPerturb(float worldX, float worldZ) const noexcept {
    const HexVec4 n = sample(worldX, worldZ);
    return (n.y * 2.f - 1.f) * HexMetrics::kElevationPerturbStrength;
}

}  // namespace eve::hexmap
