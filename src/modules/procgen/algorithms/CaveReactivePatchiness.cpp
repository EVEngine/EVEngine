#include "procgen/algorithms/CaveReactivePatchiness.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eve::procgen {
namespace {

size_t voxelIndex(int x, int y, int z, int nx, int ny) {
    return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
}

uint64_t mixBits(uint64_t value) {
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31u);
}

float latticeValue(int x, int y, int z, uint64_t seed) {
    uint64_t key = seed ^ (uint64_t(uint32_t(x)) * 0x9e3779b185ebca87ULL);
    key ^= uint64_t(uint32_t(y)) * 0xc2b2ae3d27d4eb4fULL;
    key ^= uint64_t(uint32_t(z)) * 0x165667b19e3779f9ULL;
    return float(mixBits(key) >> 40u) * (2.f / 16777215.f) - 1.f;
}

float smooth(float value) { return value * value * (3.f - 2.f * value); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float valueNoise(float x, float y, float z, float frequency, uint64_t seed) {
    x *= frequency;
    y *= frequency;
    z *= frequency;
    const int   x0 = int(std::floor(x)), y0 = int(std::floor(y)), z0 = int(std::floor(z));
    const float tx = smooth(x - float(x0)), ty = smooth(y - float(y0)), tz = smooth(z - float(z0));
    const float x00 = lerp(latticeValue(x0, y0, z0, seed), latticeValue(x0 + 1, y0, z0, seed), tx);
    const float x10 = lerp(latticeValue(x0, y0 + 1, z0, seed), latticeValue(x0 + 1, y0 + 1, z0, seed), tx);
    const float x01 = lerp(latticeValue(x0, y0, z0 + 1, seed), latticeValue(x0 + 1, y0, z0 + 1, seed), tx);
    const float x11 = lerp(latticeValue(x0, y0 + 1, z0 + 1, seed), latticeValue(x0 + 1, y0 + 1, z0 + 1, seed), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

float dot(const CaveHydrologyVec3& a, const CaveHydrologyVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

CaveHydrologyVec3 normalize(CaveHydrologyVec3 value) {
    const float length = std::sqrt(dot(value, value));
    if (length <= 1e-6f) return {1.f, 0.f, 0.f};
    return {value.x / length, value.y / length, value.z / length};
}

CaveHydrologyVec3 add(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

CaveHydrologyVec3 mul(CaveHydrologyVec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

CaveHydrologyVec3 transverse(CaveHydrologyVec3 tangent) {
    CaveHydrologyVec3 value{-tangent.z, 0.f, tangent.x};
    if (dot(value, value) <= 1e-6f) value = {1.f, 0.f, 0.f};
    return normalize(value);
}

CaveHydrologyVec3 flowWarp(CaveHydrologyVec3 position, CaveHydrologyVec3 tangent) {
    constexpr float longitudinalScale = 0.28f;
    tangent                           = normalize(tangent);
    return add(position, mul(tangent, dot(position, tangent) * (longitudinalScale - 1.f)));
}

float patchRate(CaveHydrologyVec3 position, CaveHydrologyVec3 tangent, uint64_t seed) {
    const CaveHydrologyVec3 warped     = flowWarp(position, tangent);
    const float             broad      = valueNoise(warped.x, warped.y, warped.z, 3.25f, seed ^ 0x7265616374697665ULL);
    const float             detail     = valueNoise(warped.x, warped.y, warped.z, 7.5f, seed ^ 0x7061746368657321ULL);
    const float             spectrum   = std::clamp(broad * 0.72f + detail * 0.28f, -1.f, 1.f);
    const float             normalized = smooth(spectrum * 0.5f + 0.5f);
    return 0.35f + normalized * 1.65f;
}

}  // namespace

CaveReactivePatchinessResult evolveCaveSurfaceByCorrelatedReactivity(std::vector<float>&                   density,
                                                                     const std::vector<float>&             rateField,
                                                                     const std::vector<CaveHydrologyVec3>& flowField,
                                                                     int nx, int ny, int nz, float strength,
                                                                     uint64_t seed, int iterations) {
    CaveReactivePatchinessResult result;
    if (strength <= 0.f || iterations <= 0 || nx < 5 || ny < 5 || nz < 5 || density.size() != rateField.size() ||
        density.size() != flowField.size() || density.size() != size_t(nx) * size_t(ny) * size_t(nz))
        return result;

    const float          hx = 2.f / float(nx - 1), hy = 2.f / float(ny - 1), hz = 2.f / float(nz - 1);
    const float          cellScale = std::min({hx, hy, hz});
    const float          band      = cellScale * 2.5f;
    std::vector<float>   source(density.size());
    std::vector<uint8_t> affected(density.size(), uint8_t(0));
    float                coherenceTotal           = 0.f;
    float                flowCoherenceTotal       = 0.f;
    float                transverseCoherenceTotal = 0.f;
    int                  coherenceSamples         = 0;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        source = density;
        for (int z = 2; z < nz - 2; ++z) {
            for (int y = 2; y < ny - 2; ++y) {
                for (int x = 2; x < nx - 2; ++x) {
                    const size_t center = voxelIndex(x, y, z, nx, ny);
                    const float  value  = source[center];
                    if (std::fabs(value) > band) continue;
                    const float             px = float(x) / float(nx - 1) * 2.f - 1.f;
                    const float             py = float(y) / float(ny - 1) * 2.f - 1.f;
                    const float             pz = float(z) / float(nz - 1) * 2.f - 1.f;
                    const CaveHydrologyVec3 position{px, py, pz};
                    const CaveHydrologyVec3 flow              = normalize(flowField[center]);
                    const CaveHydrologyVec3 across            = transverse(flow);
                    const float             localPatchRate    = patchRate(position, flow, seed);
                    const float             adjacentPatchRate = patchRate({px + hx, py, pz}, flow, seed);
                    const float             flowPatchRate = patchRate(add(position, mul(flow, cellScale)), flow, seed);
                    const float transversePatchRate = patchRate(add(position, mul(across, cellScale)), flow, seed);
                    coherenceTotal += 1.f - std::min(std::fabs(localPatchRate - adjacentPatchRate) / 1.65f, 1.f);
                    flowCoherenceTotal += 1.f - std::min(std::fabs(localPatchRate - flowPatchRate) / 1.65f, 1.f);
                    transverseCoherenceTotal +=
                        1.f - std::min(std::fabs(localPatchRate - transversePatchRate) / 1.65f, 1.f);
                    ++coherenceSamples;

                    const float surfaceWeight    = 1.f - std::clamp(std::fabs(value) / band, 0.f, 1.f);
                    const float accessRate       = std::clamp(rateField[center], 0.25f, 2.5f);
                    const float coupledPatchRate = 1.f + strength * (localPatchRate - 1.f);
                    const float retreat          = strength * cellScale * 0.032f * surfaceWeight * accessRate *
                                                   coupledPatchRate / float(iterations);
                    if (retreat <= 1e-7f) continue;
                    density[center] -= retreat;
                    if (affected[center] == 0) {
                        affected[center] = 1;
                        ++result.affectedVoxels;
                    }
                    result.maximumRetreat = std::max(result.maximumRetreat, retreat);
                    result.totalRetreat += retreat;
                    result.minimumPatchRate = std::min(result.minimumPatchRate, coupledPatchRate);
                    result.maximumPatchRate = std::max(result.maximumPatchRate, coupledPatchRate);
                }
            }
        }
    }
    if (coherenceSamples > 0) {
        result.meanNeighborCoherence     = coherenceTotal / float(coherenceSamples);
        result.meanFlowCoherence         = flowCoherenceTotal / float(coherenceSamples);
        result.meanTransverseCoherence   = transverseCoherenceTotal / float(coherenceSamples);
        const float flowDifference       = std::max(1.f - result.meanFlowCoherence, 1e-6f);
        const float transverseDifference = std::max(1.f - result.meanTransverseCoherence, 1e-6f);
        result.channelAnisotropy         = transverseDifference / flowDifference;
    }
    return result;
}

}  // namespace eve::procgen
