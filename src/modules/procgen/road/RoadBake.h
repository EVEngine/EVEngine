#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadTypes.h"

namespace eve::procgen::road {

/** @brief Options for baking a road network into mesh + navigation overlays. */
struct RoadBakeOptions {
    int   pathSegmentsPerEdge = 48;
    int   turnSamples         = 12;
    bool  includePiers        = true;
    bool  includeMarkings     = true;
    bool  includeNavigation   = true;
    bool  includeJunctions    = true;
    float navRibbonHalfWidth  = 0.06f;
    float arrowSpacing        = 6.f;
};

/** @brief Owning bake result: triangle mesh groups plus overlay polylines. */
struct RoadBakeResult {
    MeshBuild   mesh;
    RoadOverlay overlay;
};

/**
 * @brief Bake every edge, junction platform, pier, marking and navigation overlay.
 * @param network Borrowed live network; must outlive the call only.
 * @param options Bake knobs; segment counts must be positive.
 * @return Owning mesh/overlay snapshot, or a structured diagnostic.
 * @note Synchronous, owner-thread-only; result retains no pointers into network.
 */
[[nodiscard]] Result<RoadBakeResult> bakeRoadNetwork(const RoadNetwork& network,
                                                     const RoadBakeOptions& options = {});

}  // namespace eve::procgen::road
