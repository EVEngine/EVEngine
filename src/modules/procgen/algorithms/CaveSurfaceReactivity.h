#pragma once

#include <vector>

namespace eve::procgen {

/** @brief Diagnostics from geometry-controlled cave-surface dissolution. */
struct CaveSurfaceReactivityResult {
    int   affectedVoxels          = 0;
    float maximumRetreat          = 0.f;
    float totalRetreat            = 0.f;
    float maximumNormalDispersion = 0.f;
    float minimumRateMultiplier   = 1.f;
    float maximumRateMultiplier   = 1.f;
};

/**
 * @brief Retreat rough, highly reactive surface sites using rotation-invariant normal dispersion.
 * @param density Signed density field, negative in cave air and positive in rock.
 * @param rateField Per-voxel reactive-access multiplier.
 * @param nx X-axis sample count.
 * @param ny Y-axis sample count.
 * @param nz Z-axis sample count.
 * @param strength Opt-in coupling strength in [0, 1].
 * @param iterations Number of geometry-feedback iterations.
 * @return Deterministic retreat and local roughness diagnostics.
 */
[[nodiscard]] CaveSurfaceReactivityResult evolveCaveSurfaceByReactivity(std::vector<float>&       density,
                                                                        const std::vector<float>& rateField, int nx,
                                                                        int ny, int nz, float strength,
                                                                        int iterations = 2);

}  // namespace eve::procgen
