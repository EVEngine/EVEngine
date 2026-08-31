#pragma once

#include "procgen/algorithms/CaveScallops.h"

namespace eve::procgen {

/** @brief Inputs for a younger, spatially bounded cave-scallop erosion stage. */
struct CaveScallopHistoryInput {
    CaveScallopInput base;
    float            historyStrength = 0.f;
};

/** @brief Observable combination of preserved older and younger scallop generations. */
struct CaveScallopHistorySample {
    float erosion             = 0.f;
    float youngerErosion      = 0.f;
    float youngerCoverage     = 0.f;
    float reversalMask        = 0.f;
    float secondaryScaleRatio = 1.f;
};

/**
 * @brief Sample cumulative erosion from two spatially partitioned cave-flow stages.
 * @param input Original scallop inputs plus normalized younger-stage strength.
 * @return Cumulative retreat and diagnostics for younger coverage, scale, and reversal.
 */
[[nodiscard]] CaveScallopHistorySample sampleCaveScallopHistory(const CaveScallopHistoryInput& input);

}  // namespace eve::procgen
