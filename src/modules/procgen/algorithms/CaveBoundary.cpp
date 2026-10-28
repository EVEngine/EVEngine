#include "procgen/algorithms/CaveBoundary.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eve::procgen {
namespace {

bool boundarySample(int x, int y, int z, int nx, int ny, int nz) {
    return x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == nz - 1;
}

int countBoundaryAir(const std::vector<float>& density, int nx, int ny, int nz) {
    int count = 0;
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (boundarySample(x, y, z, nx, ny, nz) &&
                    density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)] < 0.f)
                    ++count;
    return count;
}

}  // namespace

CaveBoundaryClosure closeCaveDensityBoundary(std::vector<float>& density, int nx, int ny, int nz, float strength,
                                             uint32_t seed) {
    CaveBoundaryClosure result;
    result.airSamplesBefore = countBoundaryAir(density, nx, ny, nz);
    if (strength <= 0.f) {
        result.airSamplesAfter = result.airSamplesBefore;
        return result;
    }
    const float baseDepth = 0.035f + 0.065f * std::clamp(strength, 0.f, 1.f);
    const float phase     = float(seed & 0xffffu) * (6.28318530718f / 65536.f);
    for (int z = 0; z < nz; ++z) {
        const float pz = float(z) / float(nz - 1) * 2.f - 1.f;
        for (int y = 0; y < ny; ++y) {
            const float py = float(y) / float(ny - 1) * 2.f - 1.f;
            for (int x = 0; x < nx; ++x) {
                const float px             = float(x) / float(nx - 1) * 2.f - 1.f;
                const float inwardDistance = 1.f - std::max({std::fabs(px), std::fabs(py), std::fabs(pz)});
                const float roughness      = 1.f + 0.22f * std::sin(px * 11.3f + py * 7.1f + phase) *
                                                       std::sin(pz * 9.7f - py * 5.3f + phase * 0.73f);
                const float envelope       = baseDepth * roughness - inwardDistance;
                float&      value = density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)];
                if (envelope > value) {
                    value = envelope;
                    ++result.changedVoxels;
                }
            }
        }
    }
    result.airSamplesAfter = countBoundaryAir(density, nx, ny, nz);
    return result;
}

}  // namespace eve::procgen
