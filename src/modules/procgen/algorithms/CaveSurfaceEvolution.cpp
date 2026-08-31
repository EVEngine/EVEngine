#include "procgen/algorithms/CaveSurfaceEvolution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eve::procgen {
namespace {

size_t voxelIndex(int x, int y, int z, int nx, int ny) {
    return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
}

}  // namespace

CaveSurfaceEvolutionResult evolveSurface(std::vector<float>& density, const std::vector<float>* rateField, int nx,
                                         int ny, int nz, float strength, int iterations) {
    CaveSurfaceEvolutionResult result;
    if (strength <= 0.f || iterations <= 0 || nx < 3 || ny < 3 || nz < 3 ||
        density.size() != size_t(nx) * size_t(ny) * size_t(nz) ||
        (rateField != nullptr && rateField->size() != density.size()))
        return result;

    const float          hx = 2.f / float(nx - 1), hy = 2.f / float(ny - 1), hz = 2.f / float(nz - 1);
    const float          cellScale = std::min({hx, hy, hz});
    const float          band      = cellScale * 2.5f;
    std::vector<float>   source(density.size());
    std::vector<uint8_t> affected(density.size(), uint8_t(0));

    for (int iteration = 0; iteration < iterations; ++iteration) {
        source = density;
        for (int z = 1; z < nz - 1; ++z) {
            for (int y = 1; y < ny - 1; ++y) {
                for (int x = 1; x < nx - 1; ++x) {
                    const size_t center = voxelIndex(x, y, z, nx, ny);
                    const float  value  = source[center];
                    if (std::fabs(value) > band) continue;

                    auto at = [&](int ox, int oy, int oz) {
                        return source[voxelIndex(x + ox, y + oy, z + oz, nx, ny)];
                    };
                    const float gx        = (at(1, 0, 0) - at(-1, 0, 0)) / (2.f * hx);
                    const float gy        = (at(0, 1, 0) - at(0, -1, 0)) / (2.f * hy);
                    const float gz        = (at(0, 0, 1) - at(0, 0, -1)) / (2.f * hz);
                    const float gradient2 = gx * gx + gy * gy + gz * gz;
                    if (gradient2 < 1e-8f) continue;

                    const float hxx = (at(1, 0, 0) - 2.f * value + at(-1, 0, 0)) / (hx * hx);
                    const float hyy = (at(0, 1, 0) - 2.f * value + at(0, -1, 0)) / (hy * hy);
                    const float hzz = (at(0, 0, 1) - 2.f * value + at(0, 0, -1)) / (hz * hz);
                    const float hxy = (at(1, 1, 0) - at(1, -1, 0) - at(-1, 1, 0) + at(-1, -1, 0)) / (4.f * hx * hy);
                    const float hxz = (at(1, 0, 1) - at(1, 0, -1) - at(-1, 0, 1) + at(-1, 0, -1)) / (4.f * hx * hz);
                    const float hyz = (at(0, 1, 1) - at(0, 1, -1) - at(0, -1, 1) + at(0, -1, -1)) / (4.f * hy * hz);
                    const float normalHessian          = (gx * gx * hxx + gy * gy * hyy + gz * gz * hzz +
                                                          2.f * (gx * gy * hxy + gx * gz * hxz + gy * gz * hyz)) /
                                                         gradient2;
                    const float curvatureTimesGradient = hxx + hyy + hzz - normalHessian;

                    // The field is negative in cave air and positive in rock. Positive
                    // curvature therefore identifies exposed convex cavity walls. Retreat
                    // those sites, while leaving concave shelters and sharp scallop crests
                    // to the directional erosion model.
                    const float normalizedCurvature = std::max(0.f, curvatureTimesGradient * cellScale);
                    if (normalizedCurvature <= 1e-5f) continue;
                    const float surfaceWeight = 1.f - std::clamp(std::fabs(value) / band, 0.f, 1.f);
                    const float rateMultiplier =
                        rateField == nullptr ? 1.f : std::clamp((*rateField)[center], 0.25f, 2.5f);
                    const float retreat = strength * cellScale * 0.12f * surfaceWeight *
                                          std::min(normalizedCurvature, 1.5f) * rateMultiplier / float(iterations);
                    density[center] -= retreat;
                    if (affected[center] == 0) {
                        affected[center] = 1;
                        ++result.affectedVoxels;
                    }
                    result.maximumRetreat = std::max(result.maximumRetreat, retreat);
                    result.totalRetreat += retreat;
                    result.minimumRateMultiplier = std::min(result.minimumRateMultiplier, rateMultiplier);
                    result.maximumRateMultiplier = std::max(result.maximumRateMultiplier, rateMultiplier);
                }
            }
        }
    }
    return result;
}

CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>& density, int nx, int ny, int nz,
                                                        float strength, int iterations) {
    return evolveSurface(density, nullptr, nx, ny, nz, strength, iterations);
}

CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>&       density,
                                                        const std::vector<float>& rateField, int nx, int ny, int nz,
                                                        float strength, int iterations) {
    return evolveSurface(density, &rateField, nx, ny, nz, strength, iterations);
}

}  // namespace eve::procgen
