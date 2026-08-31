#include "procgen/algorithms/CaveSurfaceReactivity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eve::procgen {
namespace {

struct Normal {
    float x     = 0.f;
    float y     = 0.f;
    float z     = 0.f;
    bool  valid = false;
};

size_t voxelIndex(int x, int y, int z, int nx, int ny) {
    return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
}

Normal densityNormal(const std::vector<float>& density, int x, int y, int z, int nx, int ny, float hx, float hy,
                     float hz) {
    const float gx = (density[voxelIndex(x + 1, y, z, nx, ny)] - density[voxelIndex(x - 1, y, z, nx, ny)]) / (2.f * hx);
    const float gy = (density[voxelIndex(x, y + 1, z, nx, ny)] - density[voxelIndex(x, y - 1, z, nx, ny)]) / (2.f * hy);
    const float gz = (density[voxelIndex(x, y, z + 1, nx, ny)] - density[voxelIndex(x, y, z - 1, nx, ny)]) / (2.f * hz);
    const float length = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (length <= 1e-6f) return {};
    return {gx / length, gy / length, gz / length, true};
}

}  // namespace

CaveSurfaceReactivityResult evolveCaveSurfaceByReactivity(std::vector<float>&       density,
                                                          const std::vector<float>& rateField, int nx, int ny, int nz,
                                                          float strength, int iterations) {
    CaveSurfaceReactivityResult result;
    if (strength <= 0.f || iterations <= 0 || nx < 5 || ny < 5 || nz < 5 || density.size() != rateField.size() ||
        density.size() != size_t(nx) * size_t(ny) * size_t(nz))
        return result;

    const float hx = 2.f / float(nx - 1), hy = 2.f / float(ny - 1), hz = 2.f / float(nz - 1);
    const float cellScale                           = std::min({hx, hy, hz});
    const float band                                = cellScale * 2.5f;
    const std::array<std::array<int, 3>, 6> offsets = {
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::vector<float>   source(density.size());
    std::vector<uint8_t> affected(density.size(), uint8_t(0));

    for (int iteration = 0; iteration < iterations; ++iteration) {
        source = density;
        for (int z = 2; z < nz - 2; ++z) {
            for (int y = 2; y < ny - 2; ++y) {
                for (int x = 2; x < nx - 2; ++x) {
                    const size_t center = voxelIndex(x, y, z, nx, ny);
                    const float  value  = source[center];
                    if (std::fabs(value) > band) continue;
                    const Normal centerNormal = densityNormal(source, x, y, z, nx, ny, hx, hy, hz);
                    if (!centerNormal.valid) continue;

                    float dispersion = 0.f;
                    int   samples    = 0;
                    for (const auto& offset : offsets) {
                        const int sx = x + offset[0], sy = y + offset[1], sz = z + offset[2];
                        if (std::fabs(source[voxelIndex(sx, sy, sz, nx, ny)]) > band * 1.5f) continue;
                        const Normal neighbor = densityNormal(source, sx, sy, sz, nx, ny, hx, hy, hz);
                        if (!neighbor.valid) continue;
                        const float alignment = std::fabs(centerNormal.x * neighbor.x + centerNormal.y * neighbor.y +
                                                          centerNormal.z * neighbor.z);
                        dispersion += 1.f - std::clamp(alignment, 0.f, 1.f);
                        ++samples;
                    }
                    if (samples < 2) continue;
                    const float normalDispersion = std::clamp(std::sqrt(dispersion / float(samples)) * 1.8f, 0.f, 1.f);
                    if (normalDispersion <= 1e-4f) continue;

                    const float surfaceWeight  = 1.f - std::clamp(std::fabs(value) / band, 0.f, 1.f);
                    const float rateMultiplier = std::clamp(rateField[center], 0.25f, 2.5f);
                    const float retreat        = strength * cellScale * 0.045f * surfaceWeight * normalDispersion *
                                                 rateMultiplier / float(iterations);
                    density[center] -= retreat;
                    if (affected[center] == 0) {
                        affected[center] = 1;
                        ++result.affectedVoxels;
                    }
                    result.maximumRetreat = std::max(result.maximumRetreat, retreat);
                    result.totalRetreat += retreat;
                    result.maximumNormalDispersion = std::max(result.maximumNormalDispersion, normalDispersion);
                    result.minimumRateMultiplier   = std::min(result.minimumRateMultiplier, rateMultiplier);
                    result.maximumRateMultiplier   = std::max(result.maximumRateMultiplier, rateMultiplier);
                }
            }
        }
    }
    return result;
}

}  // namespace eve::procgen
