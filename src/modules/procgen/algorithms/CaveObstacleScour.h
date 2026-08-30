#pragma once

#include "procgen/algorithms/CaveBreakdown.h"
#include "procgen/algorithms/CaveHydrology.h"

#include <vector>

namespace eve::procgen {

/** @brief One landed breakdown block coupled to a horseshoe-vortex and wake scour footprint. */
struct CaveObstacleScourSite {
    CaveHydrologyVec3 blockCenter;
    CaveHydrologyVec3 frontCenter;
    CaveHydrologyVec3 wakeCenter;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
    CaveHydrologyVec3 lateral{0.f, 0.f, 1.f};
    float             frontAlongRadius   = 0.1f;
    float             frontLateralRadius = 0.1f;
    float             depthRadius        = 0.05f;
    float             wakeAlongRadius    = 0.15f;
    float             erosionPotential   = 0.f;
    float             roughnessRetention = 1.f;
};

/** @brief Local upstream horseshoe and downstream wake erosion response. */
struct CaveObstacleScourSample {
    float erosion            = 0.f;
    float horseshoeScour     = 0.f;
    float wakeScour          = 0.f;
    float roughnessRetention = 1.f;
    int   siteIndex          = -1;
};

/**
 * @brief Couple landed breakdown blocks to the nearest authoritative cave-stream segment.
 * @param breakdown Existing paired ceiling-collapse and landed-block set.
 * @param trunk Main cave-stream path ordered downstream.
 * @param hydraulicWeights Per-segment authoritative hydraulic exposure.
 * @param sedimentLoad Normalized mobile sediment supply with tools-and-cover competition.
 * @param wallRoughness Normalized multi-scale bed roughness that disrupts coherent horseshoe vortices.
 * @return Deterministic scour sites; blocks outside the active passage footprint are ignored.
 */
[[nodiscard]] std::vector<CaveObstacleScourSite> createCaveObstacleScourSites(
    const CaveBreakdownSet& breakdown, const std::vector<CaveHydrologyPoint>& trunk,
    const std::vector<float>& hydraulicWeights, float sedimentLoad, float wallRoughness);

/**
 * @brief Sample deep upstream horseshoe scour and shallower elongated wake erosion.
 * @param point Normalized cave-space point.
 * @param sites Sites derived by createCaveObstacleScourSites.
 * @return Maximum bounded obstacle-scour response.
 */
[[nodiscard]] CaveObstacleScourSample sampleCaveObstacleScour(CaveHydrologyVec3                         point,
                                                              const std::vector<CaveObstacleScourSite>& sites);

}  // namespace eve::procgen
