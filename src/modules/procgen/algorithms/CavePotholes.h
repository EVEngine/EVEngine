#pragma once

#include "procgen/algorithms/CaveFractures.h"
#include "procgen/algorithms/CaveHydrology.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

/** @brief One fracture-seeded, sediment-driven eddy pothole on a cave-stream bed. */
struct CavePotholeSite {
    CaveHydrologyVec3 center;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
    CaveHydrologyVec3 lateral{0.f, 0.f, 1.f};
    float             alongRadius          = 0.1f;
    float             lateralRadius        = 0.1f;
    float             depthRadius          = 0.1f;
    float             fractureIntersection = 0.f;
    float             erosionPotential     = 0.f;
    float             gravelSize           = 0.5f;
};

/** @brief Local primary and compound pothole erosion response. */
struct CavePotholeSample {
    float erosion            = 0.f;
    float wallAndBedAbrasion = 0.f;
    float secondaryPothole   = 0.f;
    float downstreamBias     = 0.f;
    int   siteIndex          = -1;
};

/**
 * @brief Derive eddy-pothole sites from intersections of existing fracture planes along the trunk bed.
 * @param trunk Main cave-stream path ordered downstream.
 * @param hydraulicWeights Per-segment authoritative hydraulic exposure.
 * @param fractures Existing deterministic cave fracture planes.
 * @param apertureVariability Spatial aperture variability used by the fracture field.
 * @param stressControl Stress-controlled fracture branching strength.
 * @param sedimentLoad Normalized mobile sediment supply with tools-and-cover competition.
 * @param gravelSize Normalized grinder size; fine tools spread downstream and coarse tools localize upstream.
 * @param seed Deterministic recipe seed shared with the fracture field.
 * @return Spaced pothole sites; no site is invented without a crossing-fracture weak zone.
 */
[[nodiscard]] std::vector<CavePotholeSite> createCavePotholeSites(const std::vector<CaveHydrologyPoint>& trunk,
                                                                  const std::vector<float>&        hydraulicWeights,
                                                                  const std::vector<CaveFracture>& fractures,
                                                                  float apertureVariability, float stressControl,
                                                                  float sedimentLoad, float gravelSize, uint32_t seed);

/**
 * @brief Sample gravel-size-dependent pothole abrasion around derived sites.
 * @param point Normalized cave-space point.
 * @param sites Sites derived by createCavePotholeSites.
 * @return Maximum bounded primary or compound-pothole erosion response.
 */
[[nodiscard]] CavePotholeSample sampleCavePotholeErosion(CaveHydrologyVec3                   point,
                                                         const std::vector<CavePotholeSite>& sites);

}  // namespace eve::procgen
