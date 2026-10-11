#pragma once
#include "common/Export.h"


#include <vector>

namespace eve::procgen {

/** @brief CaveFieldPoint public API. */
struct CaveFieldPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief CaveResampledField public API. */
struct CaveResampledField {
    std::vector<float> density;
    int                nx = 0;
    int                ny = 0;
    int                nz = 0;
};

/** @brief Sample cave density. */
EVENGINE_API_DOMAINS float          sampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz,
                                                      CaveFieldPoint point);
/** @brief Sample cave density gradient. */
EVENGINE_API_DOMAINS CaveFieldPoint sampleCaveDensityGradient(const std::vector<float>& density, int nx, int ny, int nz,
                                                              CaveFieldPoint point);
/** @brief Project to cave density surface. */
CaveFieldPoint projectToCaveDensitySurface(CaveFieldPoint meshPoint, const std::vector<float>& density, int nx, int ny,
                                           int nz);
/** @brief Resample cave density. */
EVENGINE_API_DOMAINS CaveResampledField resampleCaveDensity(const std::vector<float>& density, int nx, int ny, int nz,
                                                            int factor);

}  // namespace eve::procgen
