#pragma once

#include <cstdint>
#include <vector>

#include "procgen/algorithms/CaveHydrology.h"

namespace eve::procgen {

/** @brief Diagnostics from spatially correlated heterogeneous cave-wall dissolution. */
struct CaveReactivePatchinessResult {
    int   affectedVoxels          = 0;
    float maximumRetreat          = 0.f;
    float totalRetreat            = 0.f;
    float minimumPatchRate        = 1.f;
    float maximumPatchRate        = 1.f;
    float meanNeighborCoherence   = 0.f;
    float meanFlowCoherence       = 0.f;
    float meanTransverseCoherence = 0.f;
    float channelAnisotropy       = 1.f;
};

/**
 * @brief Retreat a cave surface through a deterministic, multiscale correlated reactivity field.
 * @param density Exclusively borrowed signed density field; negative values are cave air and positive values rock.
 * @param rateField Synchronously borrowed per-voxel reactive-access multiplier with the same extent as density.
 * @param flowField Synchronously borrowed normalized local passage-flow directions with the same extent as density.
 * @param nx X-axis sample count.
 * @param ny Y-axis sample count.
 * @param nz Z-axis sample count.
 * @param strength Opt-in heterogeneous-reactivity strength in [0, 1].
 * @param seed Explicit deterministic seed; no global RNG or wall-clock state is read.
 * @param iterations Number of surface-feedback iterations.
 * @return Owning diagnostics. The same inputs are deterministic on the same CPU floating-point backend.
 *
 * @note This synchronous CPU operation has no thread affinity, but density must not be accessed concurrently.
 * Invalid extents are treated as an inactive operation and leave density unchanged.
 */
[[nodiscard]] CaveReactivePatchinessResult evolveCaveSurfaceByCorrelatedReactivity(
    std::vector<float>& density, const std::vector<float>& rateField, const std::vector<CaveHydrologyVec3>& flowField,
    int nx, int ny, int nz, float strength, uint64_t seed, int iterations = 2);

}  // namespace eve::procgen
