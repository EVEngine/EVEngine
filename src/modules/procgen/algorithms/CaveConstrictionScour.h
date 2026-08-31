#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <vector>

namespace eve::procgen {

/** @brief One flow-driven constriction-pool-widening sequence derived from passage geometry. */
struct CaveConstrictionScourSite {
    CaveHydrologyVec3 constriction;
    CaveHydrologyVec3 poolCenter;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
    CaveHydrologyVec3 lateral{0.f, 0.f, 1.f};
    float             alongRadius         = 0.2f;
    float             lateralRadius       = 0.1f;
    float             depthRadius         = 0.1f;
    float             constrictionRatio   = 0.f;
    float             hydraulicIntensity  = 1.f;
    float             optimalConstriction = 0.35f;
    float             plungingEfficiency  = 0.f;
};

/** @brief Local plunging-flow scour and exit-widening response. */
struct CaveConstrictionScourSample {
    float erosion            = 0.f;
    float bedScour           = 0.f;
    float exitWidening       = 0.f;
    float constrictionRatio  = 0.f;
    float plungingEfficiency = 0.f;
    int   siteIndex          = -1;
};

/**
 * @brief Derive scour sites from local radius minima in the authoritative hydrology paths.
 * @param trunk Main passage path.
 * @param branches Branch paths.
 * @param hydrology Hydraulic weights corresponding to trunk and branches.
 * @return Deterministically located constriction-pool-widening sites.
 */
[[nodiscard]] std::vector<CaveConstrictionScourSite> createCaveConstrictionScourSites(
    const std::vector<CaveHydrologyPoint>& trunk, const std::vector<CaveHydrologyBranch>& branches,
    const CaveHydrologyWeights& hydrology);

/**
 * @brief Sample bed scour downstream of a constriction and lateral erosion near the pool exit.
 * @param point Normalized cave-space point.
 * @param sites Sites derived by createCaveConstrictionScourSites.
 * @return Maximum local scour response and contributing site.
 */
[[nodiscard]] CaveConstrictionScourSample sampleCaveConstrictionScour(
    CaveHydrologyVec3 point, const std::vector<CaveConstrictionScourSite>& sites);

}  // namespace eve::procgen
