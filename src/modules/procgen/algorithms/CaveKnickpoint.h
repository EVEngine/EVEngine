#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <vector>

namespace eve::procgen {

/** @brief One slope-break-driven cave-stream knickpoint and downstream plunge pool. */
struct CaveKnickpointSite {
    CaveHydrologyVec3 lip;
    CaveHydrologyVec3 poolCenter;
    CaveHydrologyVec3 headwallCenter;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
    CaveHydrologyVec3 lateral{0.f, 0.f, 1.f};
    float             alongRadius      = 0.2f;
    float             lateralRadius    = 0.1f;
    float             depthRadius      = 0.1f;
    float             slopeBreak       = 0.f;
    float             drop             = 0.f;
    float             erosionPotential = 0.f;
};

/** @brief Local vertical drilling and headwall-undercut response at a cave-stream knickpoint. */
struct CaveKnickpointSample {
    float erosion          = 0.f;
    float verticalDrilling = 0.f;
    float headwallUndercut = 0.f;
    float slopeBreak       = 0.f;
    int   siteIndex        = -1;
};

/**
 * @brief Derive knickpoints from steepening downstream segments of the authoritative trunk path.
 * @param trunk Main cave-stream path, ordered downstream.
 * @param hydraulicWeights Per-segment hydraulic exposure.
 * @param sedimentLoad Normalized mobile-sediment supply, including high-load cover protection.
 * @return Deterministic knickpoint sites; branches are intentionally excluded because they flow toward index zero.
 */
[[nodiscard]] std::vector<CaveKnickpointSite> createCaveKnickpointSites(const std::vector<CaveHydrologyPoint>& trunk,
                                                                        const std::vector<float>& hydraulicWeights,
                                                                        float                     sedimentLoad);

/**
 * @brief Sample plunge-pool drilling and lower-headwall erosion around derived knickpoints.
 * @param point Normalized cave-space point.
 * @param sites Sites derived by createCaveKnickpointSites.
 * @return Maximum bounded knickpoint erosion response.
 */
[[nodiscard]] CaveKnickpointSample sampleCaveKnickpointErosion(CaveHydrologyVec3                      point,
                                                               const std::vector<CaveKnickpointSite>& sites);

}  // namespace eve::procgen
