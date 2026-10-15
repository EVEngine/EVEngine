#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::evolve(CaveSurfaceOperation operation) {
    // The resolved wall geometry now feeds back into intrinsic surface reactivity.
    // Normal dispersion is orientation-independent and is recomputed after each retreat.
    if (operation == CaveSurfaceOperation::Reactivity) {
        surfaceReactivity = evolveCaveSurfaceByReactivity(density, reactiveRate, nx, ny, nz, surfaceSlopeReactivity);
    }

    // Mineral-scale reactivity is spatially heterogeneous rather than voxel-white noise.
    // A seeded two-band spectrum creates coherent etch patches and feeds each retreat
    // iteration back into the current zero isosurface.
    if (operation == CaveSurfaceOperation::ReactivePatches) {
        reactivePatchEvolution = evolveCaveSurfaceByCorrelatedReactivity(density, reactiveRate, flowDirection, nx, ny,
                                                                         nz, reactivePatchiness, params.getSeed());
    }

    // Humid, weakly flushed ceiling sectors receive a separate late-stage corrosion
    // overprint before curvature retreat and secondary calcite deposition.
    if (operation == CaveSurfaceOperation::Condensation) {
        condensation =
            erodeCaveByCondensation(density, hydraulicExposure, nx, ny, nz, condensationCorrosion, params.getSeed());
    }

    // Carbonate wall retreat precedes secondary calcite deposition. Curvature-driven
    // dissolution rounds exposed convex asperities without blurring protected recesses.
    if (operation == CaveSurfaceOperation::Curvature) {
        surfaceEvolution = evolveCaveSurfaceByCurvature(density, reactiveRate, nx, ny, nz, curvatureDissolution);
    }


    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
