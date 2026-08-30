#pragma once

#include "procgen/algorithms/CaveHydrology.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

/** @brief One chemistry-weighted branch-to-trunk mixing site. */
struct CaveMixingSite {
    CaveHydrologyVec3 center;
    CaveHydrologyVec3 tangent{1.f, 0.f, 0.f};
    float             alongRadius     = 0.2f;
    float             radialRadius    = 0.1f;
    float             mixingPotential = 0.f;
};

/** @brief Local corrosion strength and the contributing mixing-water potential. */
struct CaveMixingCorrosionSample {
    float erosion         = 0.f;
    float mixingPotential = 0.f;
    int   siteIndex       = -1;
};

/**
 * @brief Derive mixing sites from authoritative branch anchors in a cave hydrology network.
 * @param trunk Main passage points.
 * @param branches Branch passages whose first point is attached to trunkAnchor.
 * @param seed Deterministic chemistry-contrast seed.
 * @return One bounded mixing site per valid branch junction.
 */
[[nodiscard]] std::vector<CaveMixingSite> createCaveMixingSites(const std::vector<CaveHydrologyPoint>&  trunk,
                                                                const std::vector<CaveHydrologyBranch>& branches,
                                                                uint32_t                                seed);

/**
 * @brief Sample localized shell retreat around branch-to-trunk mixing sites.
 * @param point Normalized cave-space point.
 * @param sites Sites derived by createCaveMixingSites.
 * @return Maximum local mixing-corrosion response and contributing site.
 */
[[nodiscard]] CaveMixingCorrosionSample sampleCaveMixingCorrosion(CaveHydrologyVec3                  point,
                                                                  const std::vector<CaveMixingSite>& sites);

}  // namespace eve::procgen
