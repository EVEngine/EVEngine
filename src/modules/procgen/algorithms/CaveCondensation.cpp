#include "procgen/algorithms/CaveCondensation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eve::procgen {
namespace {

float smoothstep(float low, float high, float value) {
    const float t = std::clamp((value - low) / (high - low), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float seededPhase(uint32_t seed, uint32_t salt) {
    uint32_t value = seed ^ salt;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return float(value & 0xffffu) * (6.28318530718f / 65536.f);
}

}  // namespace

CaveCondensationResult erodeCaveByCondensation(std::vector<float>& density, const std::vector<float>& hydraulicExposure,
                                               int nx, int ny, int nz, float strength, uint32_t seed) {
    CaveCondensationResult result;
    if (strength <= 0.f || nx < 3 || ny < 3 || nz < 3 || density.size() != hydraulicExposure.size()) return result;

    const std::vector<float> source = density;
    const float              phaseA = seededPhase(seed, 0x63a9f12du);
    const float              phaseB = seededPhase(seed, 0xb71d54a3u);
    const auto               index  = [nx, ny](int x, int y, int z) {
        return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
    };

    for (int z = 1; z < nz - 1; ++z) {
        const float pz = float(z) / float(nz - 1) * 2.f - 1.f;
        for (int y = 1; y < ny - 1; ++y) {
            const float py = float(y) / float(ny - 1) * 2.f - 1.f;
            for (int x = 1; x < nx - 1; ++x) {
                const size_t voxel = index(x, y, z);
                const float  d     = source[voxel];
                if (d < -0.045f || d > 0.14f) continue;

                const float gx             = source[index(x + 1, y, z)] - source[index(x - 1, y, z)];
                const float gy             = source[index(x, y + 1, z)] - source[index(x, y - 1, z)];
                const float gz             = source[index(x, y, z + 1)] - source[index(x, y, z - 1)];
                const float gradientLength = std::sqrt(gx * gx + gy * gy + gz * gz);
                if (gradientLength < 1e-5f) continue;

                // Rock-facing gradients point upward on ceilings. Condensation films are
                // favoured there and in weakly flushed sectors, not in active stream paths.
                const float ceiling = smoothstep(0.08f, 0.72f, gy / gradientLength);
                const float lowFlow = 1.f - smoothstep(0.75f, 1.65f, hydraulicExposure[voxel]);
                if (ceiling <= 0.f || lowFlow <= 0.f) continue;

                const float px            = float(x) / float(nx - 1) * 2.f - 1.f;
                const float humidityPatch = 0.5f + 0.5f * std::sin(px * 4.7f + pz * 3.9f + py * 1.3f + phaseA) *
                                                       std::sin(pz * 5.3f - px * 2.1f + phaseB);
                const float pitCarrier =
                    0.5f + 0.5f * std::sin(px * 31.f + pz * 23.f + phaseB) * std::sin(py * 27.f - px * 17.f + phaseA);
                const float pits    = smoothstep(0.42f, 0.9f, pitCarrier);
                const float shell   = 1.f - smoothstep(0.035f, 0.14f, std::fabs(d));
                const float retreat = std::min(0.032f, 0.032f * std::clamp(strength, 0.f, 1.f) * shell * ceiling *
                                                           lowFlow * (0.2f + 0.8f * humidityPatch) * pits);
                if (retreat <= 1e-6f) continue;
                density[voxel] -= retreat;
                ++result.affectedVoxels;
                result.maximumRetreat = std::max(result.maximumRetreat, retreat);
                result.totalRetreat += retreat;
            }
        }
    }
    return result;
}

}  // namespace eve::procgen
