#include "procgen/algorithms/CaveFieldSampling.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eve::procgen {

float sampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz, CaveFieldPoint point) {
    const float gx = std::clamp((point.x + 1.f) * 0.5f * float(nx - 1), 0.f, float(nx - 1));
    const float gy = std::clamp((point.y + 1.f) * 0.5f * float(ny - 1), 0.f, float(ny - 1));
    const float gz = std::clamp((point.z + 1.f) * 0.5f * float(nz - 1), 0.f, float(nz - 1));
    const int   x0 = int(std::floor(gx)), y0 = int(std::floor(gy)), z0 = int(std::floor(gz));
    const int   x1 = std::min(x0 + 1, nx - 1), y1 = std::min(y0 + 1, ny - 1), z1 = std::min(z0 + 1, nz - 1);
    const float tx = gx - float(x0), ty = gy - float(y0), tz = gz - float(z0);
    auto        at = [&](int x, int y, int z) {
        return density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)];
    };
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00  = lerp(at(x0, y0, z0), at(x1, y0, z0), tx);
    const float x10  = lerp(at(x0, y1, z0), at(x1, y1, z0), tx);
    const float x01  = lerp(at(x0, y0, z1), at(x1, y0, z1), tx);
    const float x11  = lerp(at(x0, y1, z1), at(x1, y1, z1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

CaveFieldPoint sampleCaveDensityGradient(const std::vector<float>& density, int nx, int ny, int nz,
                                         CaveFieldPoint point) {
    const CaveFieldPoint step{1.f / float(nx - 1), 1.f / float(ny - 1), 1.f / float(nz - 1)};
    return {(sampleCaveDensity(density, nx, ny, nz, {point.x + step.x, point.y, point.z}) -
             sampleCaveDensity(density, nx, ny, nz, {point.x - step.x, point.y, point.z})) /
                (2.f * step.x),
            (sampleCaveDensity(density, nx, ny, nz, {point.x, point.y + step.y, point.z}) -
             sampleCaveDensity(density, nx, ny, nz, {point.x, point.y - step.y, point.z})) /
                (2.f * step.y),
            (sampleCaveDensity(density, nx, ny, nz, {point.x, point.y, point.z + step.z}) -
             sampleCaveDensity(density, nx, ny, nz, {point.x, point.y, point.z - step.z})) /
                (2.f * step.z)};
}

CaveFieldPoint projectToCaveDensitySurface(CaveFieldPoint meshPoint, const std::vector<float>& density, int nx, int ny,
                                           int nz) {
    CaveFieldPoint       point{meshPoint.x * 2.f, meshPoint.y * 2.f, meshPoint.z * 2.f};
    const CaveFieldPoint step{1.f / float(nx - 1), 1.f / float(ny - 1), 1.f / float(nz - 1)};
    for (int iteration = 0; iteration < 4; ++iteration) {
        const float          value    = sampleCaveDensity(density, nx, ny, nz, point);
        const CaveFieldPoint gradient = sampleCaveDensityGradient(density, nx, ny, nz, point);
        const float          length2  = gradient.x * gradient.x + gradient.y * gradient.y + gradient.z * gradient.z;
        if (length2 < 1e-8f) break;
        point.x = std::clamp(point.x - gradient.x * value / length2, -1.f, 1.f);
        point.y = std::clamp(point.y - gradient.y * value / length2, -1.f, 1.f);
        point.z = std::clamp(point.z - gradient.z * value / length2, -1.f, 1.f);
    }
    return {point.x * 0.5f, point.y * 0.5f, point.z * 0.5f};
}

CaveResampledField resampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz, int factor) {
    CaveResampledField result;
    result.nx = (nx - 1) * factor + 1;
    result.ny = (ny - 1) * factor + 1;
    result.nz = (nz - 1) * factor + 1;
    result.density.resize(size_t(result.nx) * size_t(result.ny) * size_t(result.nz));
    for (int z = 0; z < result.nz; ++z) {
        for (int y = 0; y < result.ny; ++y) {
            for (int x = 0; x < result.nx; ++x) {
                const CaveFieldPoint point{float(x) / float(result.nx - 1) * 2.f - 1.f,
                                           float(y) / float(result.ny - 1) * 2.f - 1.f,
                                           float(z) / float(result.nz - 1) * 2.f - 1.f};
                result.density[size_t(x) + size_t(y) * size_t(result.nx) +
                               size_t(z) * size_t(result.nx) * size_t(result.ny)] =
                    sampleCaveDensity(density, nx, ny, nz, point);
            }
        }
    }
    return result;
}

}  // namespace eve::procgen
