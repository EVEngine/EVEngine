#pragma once
#include "common/Export.h"


#include <vector>

namespace eve::procgen {

/** @brief CaveSurfaceEvolutionResult public API. */
struct CaveSurfaceEvolutionResult {
    int   affectedVoxels        = 0;
    float maximumRetreat        = 0.f;
    float totalRetreat          = 0.f;
    float minimumRateMultiplier = 1.f;
    float maximumRateMultiplier = 1.f;
};

/** @brief Evolve cave surface by curvature. */
EVENGINE_API_DOMAINS CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>& density, int nx,
                                                                             int ny, int nz, float strength,
                                                                             int iterations = 2);
/** @brief Evolve cave surface by curvature. */
EVENGINE_API_DOMAINS CaveSurfaceEvolutionResult evolveCaveSurfaceByCurvature(std::vector<float>&       density,
                                                                             const std::vector<float>& rateField,
                                                                             int nx, int ny, int nz, float strength,
                                                                             int iterations = 2);

}  // namespace eve::procgen
