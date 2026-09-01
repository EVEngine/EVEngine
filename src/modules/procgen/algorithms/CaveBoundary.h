#pragma once

#include <cstdint>
#include <vector>

namespace eve::procgen {

struct CaveBoundaryClosure {
    int airSamplesBefore = 0;
    int airSamplesAfter  = 0;
    int changedVoxels    = 0;
};

/**
 * @brief Apply a deterministic host-rock envelope at the finite density domain boundary.
 * @param density Dense x-major scalar field, mutated in place.
 * @param nx Grid samples on X.
 * @param ny Grid samples on Y.
 * @param nz Grid samples on Z.
 * @param strength Closure depth in normalized cave space, in [0, 1].
 * @param seed Deterministic rough-envelope seed.
 * @return Boundary air counts before/after closure and the number of changed voxels.
 */
CaveBoundaryClosure closeCaveDensityBoundary(std::vector<float>& density, int nx, int ny, int nz, float strength,
                                             uint32_t seed);

}  // namespace eve::procgen
