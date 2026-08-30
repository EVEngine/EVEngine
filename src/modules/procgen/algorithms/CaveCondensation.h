#pragma once

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct CaveCondensationResult {
    int   affectedVoxels = 0;
    float maximumRetreat = 0.f;
    float totalRetreat   = 0.f;
};

/**
 * @brief Upscale long-term condensation corrosion into shallow ceiling-wall pitting.
 * @param density Dense x-major signed density field, mutated in place.
 * @param hydraulicExposure Per-voxel relative hydraulic exposure; high values suppress condensation corrosion.
 * @param nx Grid samples on X.
 * @param ny Grid samples on Y.
 * @param nz Grid samples on Z.
 * @param strength Corrosion intensity in [0, 1].
 * @param seed Deterministic microclimate and pitting seed.
 * @return Affected voxel count and bounded retreat statistics in normalized cave space.
 */
CaveCondensationResult erodeCaveByCondensation(std::vector<float>& density, const std::vector<float>& hydraulicExposure,
                                               int nx, int ny, int nz, float strength, uint32_t seed);

}  // namespace eve::procgen
