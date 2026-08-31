#pragma once

#include <vector>

namespace eve::procgen {

struct CaveFieldPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct CaveResampledField {
    std::vector<float> density;
    int                nx = 0;
    int                ny = 0;
    int                nz = 0;
};

float          sampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz, CaveFieldPoint point);
CaveFieldPoint sampleCaveDensityGradient(const std::vector<float>& density, int nx, int ny, int nz,
                                         CaveFieldPoint point);
CaveFieldPoint projectToCaveDensitySurface(CaveFieldPoint meshPoint, const std::vector<float>& density, int nx, int ny,
                                           int nz);
CaveResampledField resampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz, int factor);

}  // namespace eve::procgen
