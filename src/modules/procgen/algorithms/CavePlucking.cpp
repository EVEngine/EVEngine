#include "procgen/algorithms/CavePlucking.h"

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

uint32_t cellKey(int value) { return value >= 0 ? uint32_t(value) * 2u : uint32_t(-value) * 2u - 1u; }

}  // namespace

CavePluckingSample sampleCaveFloodPlucking(const CavePluckingInput& input) {
    CavePluckingSample result;
    const float        primary   = std::clamp(input.primaryFractureMask, 0.f, 1.f);
    const float        secondary = std::clamp(input.secondaryFractureMask, 0.f, primary);
    // One strongly exposed discontinuity can release a slab; intersecting planes
    // further increase the chance of a hydraulically extractable block.
    result.fracturePredisposition = std::clamp(primary * 0.58f + std::sqrt(primary * secondary) * 0.62f, 0.f, 1.f);
    if (result.fracturePredisposition <= 0.f) return result;

    result.hydraulicActivation = smoothstep(0.48f, 1.05f, input.hydraulicIntensity);
    const float nearBed        = std::clamp(std::pow(std::max(0.f, -std::cos(input.angle)), 1.2f) +
                                                std::pow(std::fabs(std::sin(input.angle)), 1.7f) * 0.24f,
                                            0.f, 1.f);

    const float    scale  = std::max(input.blockScale, 0.02f);
    const float    gx     = input.x / scale;
    const float    gy     = input.y / (scale * 0.72f);
    const float    gz     = input.z / (scale * 1.18f);
    const int      ix     = int(std::floor(gx));
    const int      iy     = int(std::floor(gy));
    const int      iz     = int(std::floor(gz));
    const float    lx     = std::fabs((gx - std::floor(gx)) * 2.f - 1.f);
    const float    ly     = std::fabs((gy - std::floor(gy)) * 2.f - 1.f);
    const float    lz     = std::fabs((gz - std::floor(gz)) * 2.f - 1.f);
    const float    cuboid = 1.f - smoothstep(0.58f, 0.94f, std::max({lx, ly, lz}));
    const uint32_t key =
        hash(input.seed ^ cellKey(ix) * 0x9e3779b9u ^ cellKey(iy) * 0x85ebca6bu ^ cellKey(iz) * 0xc2b2ae35u);
    const float cellSelection = smoothstep(0.62f, 0.88f, float(key & 0xffffu) / 65535.f);
    result.blockMask          = cuboid * cellSelection;

    const float release = smoothstep(0.22f, 0.78f, result.fracturePredisposition * result.hydraulicActivation);
    result.erosion      = std::clamp(nearBed * result.blockMask * release, 0.f, 1.f);
    return result;
}

}  // namespace eve::procgen
