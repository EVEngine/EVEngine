#include "procgen/algorithms/CaveBreakdown.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace eve::procgen {
namespace {

constexpr float Pi = 3.1415926535f;

float ellipsoidDistance(float x, float y, float z, const CaveBreakdownEvent& event) {
    const float c  = std::cos(event.yaw);
    const float s  = std::sin(event.yaw);
    const float dx = x - event.x;
    const float dz = z - event.z;
    const float u  = dx * c + dz * s;
    const float v  = -dx * s + dz * c;
    const float q  = std::sqrt((u * u) / (event.scarX * event.scarX) +
                               ((y - event.ceilingY) * (y - event.ceilingY)) / (event.scarY * event.scarY) +
                               (v * v) / (event.scarZ * event.scarZ));
    return (q - 1.f) * std::min({event.scarX, event.scarY, event.scarZ});
}

float roundedBoxDistance(float x, float y, float z, const CaveBreakdownBlock& block) {
    const float c       = std::cos(block.yaw);
    const float s       = std::sin(block.yaw);
    const float dx      = x - block.x;
    const float dz      = z - block.z;
    const float u       = std::fabs(dx * c + dz * s) - block.hx;
    const float v       = std::fabs(y - block.y) - block.hy;
    const float w       = std::fabs(-dx * s + dz * c) - block.hz;
    const float outside = std::sqrt(std::max(u, 0.f) * std::max(u, 0.f) + std::max(v, 0.f) * std::max(v, 0.f) +
                                    std::max(w, 0.f) * std::max(w, 0.f));
    return outside + std::min(std::max({u, v, w}), 0.f) - 0.006f;
}

}  // namespace

CaveBreakdownSet createCaveBreakdown(const std::vector<CaveBreakdownChamber>& chambers, int eventCount, float strength,
                                     uint32_t seed) {
    CaveBreakdownSet result;
    if (strength <= 0.f || eventCount <= 0 || chambers.empty()) return result;

    std::mt19937                          rng(seed ^ 0x91e10da5u);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    const float                           boundedStrength = std::clamp(strength, 0.f, 1.f);
    result.events.reserve(size_t(eventCount));
    for (int i = 0; i < eventCount; ++i) {
        const CaveBreakdownChamber& chamber = chambers[size_t(rng() % uint32_t(chambers.size()))];
        CaveBreakdownEvent          event;
        event.x        = chamber.x + (unit(rng) * 2.f - 1.f) * chamber.rx * 0.38f;
        event.z        = chamber.z + (unit(rng) * 2.f - 1.f) * chamber.rz * 0.38f;
        event.ceilingY = chamber.y + chamber.ry * (0.72f + unit(rng) * 0.1f);
        event.scarX    = (0.075f + unit(rng) * 0.065f) * (0.65f + 0.35f * boundedStrength);
        event.scarY    = (0.025f + unit(rng) * 0.025f) * (0.7f + 0.3f * boundedStrength);
        event.scarZ    = (0.075f + unit(rng) * 0.065f) * (0.65f + 0.35f * boundedStrength);
        event.yaw      = unit(rng) * Pi;

        const int blockCount = 2 + int(rng() % 3u);
        event.blocks.reserve(size_t(blockCount));
        for (int blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
            CaveBreakdownBlock block;
            const float        slab = unit(rng);
            block.hx                = event.scarX * (0.22f + unit(rng) * 0.18f);
            block.hz                = event.scarZ * (0.22f + unit(rng) * 0.18f);
            block.hy                = (slab < 0.58f ? 0.28f : 0.55f) * std::min(block.hx, block.hz);
            block.x                 = event.x + (unit(rng) * 2.f - 1.f) * event.scarX * 0.65f;
            block.z                 = event.z + (unit(rng) * 2.f - 1.f) * event.scarZ * 0.65f;
            const float floorY      = chamber.y - chamber.ry * 0.92f;
            block.y                 = floorY + block.hy * 0.55f;
            block.yaw               = event.yaw + (unit(rng) * 2.f - 1.f) * 0.42f;
            event.blocks.push_back(block);
            result.depositedVolume += 8.f * block.hx * block.hy * block.hz;
            ++result.blockCount;
        }
        result.detachedVolume += 4.f / 3.f * Pi * event.scarX * event.scarY * event.scarZ;
        result.events.push_back(std::move(event));
    }
    return result;
}

float carveCaveBreakdownScars(float x, float y, float z, float current, const CaveBreakdownSet& breakdown) {
    for (const CaveBreakdownEvent& event : breakdown.events)
        current = std::min(current, ellipsoidDistance(x, y, z, event));
    return current;
}

float addCaveBreakdownBlocks(float x, float y, float z, float current, const CaveBreakdownSet& breakdown) {
    for (const CaveBreakdownEvent& event : breakdown.events)
        for (const CaveBreakdownBlock& block : event.blocks)
            current = std::max(current, -roundedBoxDistance(x, y, z, block));
    return current;
}

bool isCaveBreakdownBlockSurface(float x, float y, float z, float tolerance, const CaveBreakdownSet& breakdown) {
    for (const CaveBreakdownEvent& event : breakdown.events)
        for (const CaveBreakdownBlock& block : event.blocks)
            if (std::fabs(roundedBoxDistance(x, y, z, block)) <= tolerance) return true;
    return false;
}

}  // namespace eve::procgen
