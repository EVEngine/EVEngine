#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/PointSet.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadTypes.h"

#include <cstddef>
#include <vector>

namespace eve {
class IProcgenWorldQuery;
class IProcgenAssetBounds;
}  // namespace eve

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
    float originY = 0.f;  ///< World height is originY + sample * heightScale; appended for aggregate compatibility.
};

/**
 * @brief Exact changed samples needed to safely restore one road terrain conformance.
 * @cost Storage, copy and move-destruction work are linear in the number of changed samples.
 */
struct RoadTerrainConformReceipt {
    std::size_t workPerformed         = 0;  ///< Raster and blend visits, for a shared multi-tile transaction budget.
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
    eve::IProcgenAssetBounds* placementAssetBounds =
        nullptr;  ///< Borrowed for this bake; null resolves the optional capability once.
    eve::IProcgenWorldQuery* placementWorldQuery =
        nullptr;  ///< Borrowed for this synchronous bake only; null resolves the optional capability once.
    float         placementQueryMinY = -10000.f, placementQueryMaxY = 10000.f;  ///< Inclusive world projection window.
    std::uint64_t placementQueryMask = ~std::uint64_t{0};  ///< Provider-defined surface selection mask.
    std::size_t   maximumPlacementQueries =
        65536;                     ///< Shared network-side-anchor query budget; misses/errors fail privately.
    int pathSegmentsPerEdge = 48;  ///< Fixed-count fallback used when the owning style's samplingSpacingMeters is zero.
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
    float       samplingScale = 1.f;  ///< Metric spacing/angle multiplier in [1,64] for bounded coarser LOD builds.
    std::size_t maximumSamplingEvaluations = 65536;  ///< Shared all-edge parameter-scan work budget; zero is invalid.
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
 * @cost Geometry generation grows with output; bounded parameter scans may dominate sparse outputs, and each
 * probe scales with path point count. Prefab weld/smoothing adds logarithmic grouping per vertex.
 * Probe work and output are bounded by `maximumSamplingEvaluations`, `maximumMeshElements` and `maximumPlacements`,
 * amortized at explicit bake boundaries. Terrain projection adds one provider call per center anchor or
 * samplesPerAxis squared calls per footprint, doubled for distinct projected top/bottom layers of tilted assets.
 * TerrainNormal alignment adds one center-normal query per candidate anchor. All height and normal queries
 * share maximumPlacementQueries across the network.
 * Retained road frames are owned once per edge for this bake; SurfaceSegments placement shares their scan budget,
 * prepares linear cumulative metrics and uses logarithmic row lookup per anchor. Neighbor clearance scans retained
 * visible segments linearly. Peak frame storage grows with total retained rows, with up to two socket rows per edge.
 * Analytic Fixed/Average placement uses 512 arc probes per span and up to 4096 orientation frames per anchor;
 * this costs more than historical 32-probe LegacyCentered placement and is paid only at explicit bake boundaries.
 * @note Synchronous, owner-thread-only; result retains no pointers into network. Explicit world/asset providers
 * are borrowed until return; registered providers must remain alive and registered throughout the call.
 * Provider callbacks run without road locks and must not mutate the borrowed network, unregister themselves,
 * or reenter this bake. Projection failure publishes no partial bake result and never modifies author data.
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
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> conformHeightmapToRoad(Heightmap& terrain, const MeshBuild& roadMesh,
                                                                      const RoadTerrainConformOptions& options = {});

/**
 * @brief Atomically conform terrain and return the exact reversible sample delta.
 * @param terrain Mutable normalized heightmap; unchanged on failure.
 * @param roadMesh Immutable baked road mesh.
 * @param options World/grid mapping, blend width, bridge/tunnel rejection threshold and work budget.
 * @return Owning receipt containing row-major changed indices and before/after values.
 * @cost Same as conformHeightmapToRoad, plus storage linear in changed samples.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadTerrainConformReceipt> conformHeightmapToRoadWithReceipt(
    Heightmap& terrain, const MeshBuild& roadMesh, const RoadTerrainConformOptions& options = {});

/**
 * @brief Atomically restore a conformance receipt if none of its samples became stale.
 * @param terrain Mutable heightmap that must still contain every receipt after-value.
 * @param receipt Owning receipt returned by conformHeightmapToRoadWithReceipt.
 * @return Number of restored samples, or a structured stale/validation diagnostic.
 * @cost Linear in receipt sample count.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> restoreHeightmapFromRoad(Heightmap&                       terrain,
                                                                        const RoadTerrainConformReceipt& receipt);

}  // namespace eve::procgen::road
