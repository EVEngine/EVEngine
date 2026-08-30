#pragma once

#include <vector>

namespace eve::procgen {

struct CaveSurfaceEvolutionResult {
    int   affectedVoxels        = 0;
    float maximumRetreat        = 0.f;
    float totalRetreat          = 0.f;
    float minimumRateMultiplier = 1.f;
    float maximumRateMultiplier = 1.f;
};

CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>& density, int nx, int ny, int nz,
                                                        float strength, int iterations = 2);
CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>&       density,
                                                        const std::vector<float>& rateField, int nx, int ny, int nz,
                                                        float strength, int iterations = 2);

}  // namespace eve::procgen
