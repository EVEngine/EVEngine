#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/PointSet.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadTypes.h"

#include <cstddef>
#include <vector>

namespace eve::procgen {
class Heightmap;
}

namespace eve::procgen::road {

/** @brief World/grid mapping and bounded blending settings for road-to-terrain conformance. */
struct RoadTerrainConformOptions {
    float originX           = 0.f;
    float originZ           = 0.f;
    float cellSize          = 1.f;
    float heightScale       = 1.f;
    float blendDistance     = 2.f;
    float maxVerticalDelta  = 2.f;
    std::size_t maximumWork = 16u * 1024u * 1024u;
};

/** @brief Exact changed samples needed to safely restore one road terrain conformance. */
struct RoadTerrainConformReceipt {
    int                        width  = 0;
    int                        height = 0;
    int                        skippedBridgeSamples = 0;  ///< Cells kept below elevated road surfaces.
    int                        skippedTunnelSamples = 0;  ///< Cells whose terrain roof was kept above tunnel surfaces.
    std::vector<std::size_t>   sampleIndices;
    std::vector<float>         before;
    std::vector<float>         after;
};

/** @brief Options for baking a road network into mesh + navigation overlays. */
struct RoadBakeOptions {
    int   pathSegmentsPerEdge = 48;
    int   turnSamples         = 12;
    bool  includePiers        = true;
    bool  includeMarkings     = true;
    bool  includeNavigation   = true;
    bool  includeJunctions    = true;
    bool  includePlacements   = true;
    float navRibbonHalfWidth  = 0.22f;
    float arrowSpacing        = 5.f;
    float junctionChordError  = 0.10f;  ///< Maximum junction-return tessellation error in world metres.
    float sideObjectSpacing   = 8.f;
    float sideObjectOffset    = 0.5f;
    float sideObjectClearance = 1.5f;  ///< Minimum 3D clearance from other roads and accepted side anchors.
    int   maximumPlacements   = 65536;
    std::size_t maximumMeshElements = 8u * 1024u * 1024u;  ///< Combined vertex and index output budget.
};

/** @brief Owning bake result: mesh, overlays and generic decoration placement points. */
struct RoadBakeResult {
    MeshBuild   mesh;
    RoadOverlay overlay;
    PointSet    placements;
};

/**
 * @brief Bake every edge, junction platform, pier, marking and navigation overlay.
 * @param network Borrowed live network; must outlive the call only.
 * @param options Bake knobs; sample counts are bounded to [2,4096], and enabled navigation dimensions must be
 * finite and positive.
 * @return Owning mesh/overlay snapshot, or a structured diagnostic.
 * @cost Linear in generated geometry and bounded by `maximumMeshElements` plus `maximumPlacements`.
 * @note Synchronous, owner-thread-only; result retains no pointers into network.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadBakeResult> bakeRoadNetwork(const RoadNetwork&     network,
                                                                          const RoadBakeOptions& options = {});

/**
 * @brief Atomically conform a heightmap to nearby baked road surface triangles.
 * @param terrain Mutable normalized heightmap; unchanged on failure.
 * @param roadMesh Immutable baked road mesh.
 * @param options World/grid mapping, blend width, bridge/tunnel rejection threshold and work budget.
 * @return Number of changed samples or a structured validation/budget diagnostic.
 * @cost Linear in road triangle coverage plus covered cells times the blend-radius area; bounded by maximumWork.
 * @thread Synchronous and caller-thread-only; no callbacks are invoked.
 */
[[nodiscard]] Result<int> conformHeightmapToRoad(Heightmap& terrain, const MeshBuild& roadMesh,
                                                 const RoadTerrainConformOptions& options = {});

/**
 * @brief Atomically conform terrain and return the exact reversible sample delta.
 * @param terrain Mutable normalized heightmap; unchanged on failure.
 * @param roadMesh Immutable baked road mesh.
 * @param options World/grid mapping, blend width, bridge/tunnel rejection threshold and work budget.
 * @return Owning receipt containing row-major changed indices and before/after values.
 * @cost Same as conformHeightmapToRoad, plus storage linear in changed samples.
 */
[[nodiscard]] Result<RoadTerrainConformReceipt>
conformHeightmapToRoadWithReceipt(Heightmap& terrain, const MeshBuild& roadMesh,
                                  const RoadTerrainConformOptions& options = {});

/**
 * @brief Atomically restore a conformance receipt if none of its samples became stale.
 * @param terrain Mutable heightmap that must still contain every receipt after-value.
 * @param receipt Owning receipt returned by conformHeightmapToRoadWithReceipt.
 * @return Number of restored samples, or a structured stale/validation diagnostic.
 * @cost Linear in receipt sample count.
 */
[[nodiscard]] Result<int> restoreHeightmapFromRoad(Heightmap& terrain,
                                                   const RoadTerrainConformReceipt& receipt);

}  // namespace eve::procgen::road
