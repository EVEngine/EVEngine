#pragma once

#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "common/BorrowedRef.h"
#include "common/Diagnostic.h"
#include "procgen/road/RoadBake.h"
#include "procgen/spline/SplinePath.h"

namespace eve::procgen::road::detail {
/** @brief One bake's sole owner of retained road frames and shared scan work; no callbacks or cross-bake reuse. */
struct RoadSurfaceRows {
    std::vector<SplineFrameSample>
                frames;  ///< Full metric path with actual clipped/blended rows in its visible interval.
    std::size_t firstVisible = 0, lastVisible = 0;
};
struct RoadFrameCache {
    std::unordered_map<std::uint32_t, std::vector<SplineFrameSample>> frames;
    std::unordered_map<std::uint32_t, RoadSurfaceRows>                surfaces;
    std::size_t                                                       samplingWork = 0;
};
/** @brief Prepare once per immutable edge/options snapshot. Borrowed result expires with cache destruction.
 * Synchronous bake-owner thread only. At most 4097 frames per edge; scan work shares the network budget.
 */
[[nodiscard]] ResultRef<const std::vector<SplineFrameSample>> prepareRoadFrames(
    RoadFrameCache& cache, const SplinePath& path, const RoadEdge& edge, const RoadBakeOptions& options, float length);
/** @brief Borrowed section centers plus owned cumulative lengths, valid only during the owning bake/cache lifetime. */
struct RoadSurfacePolyline {
    std::span<const SplineFrameSample> frames;
    std::vector<double>                distances;
    float                              length       = 0;
    std::size_t                        firstVisible = 0, lastVisible = 0;
};
/** @brief Prepare a bounded piecewise road-center metric; rejects degenerate segments without publishing output. */
[[nodiscard]] Result<RoadSurfacePolyline> prepareSurfacePolyline(std::span<const SplineFrameSample> frames);
/** @brief Sample one owned frame by segment direction; endpoints extrapolate, widths clamp to endpoint values.
 * @cost Logarithmic in retained road rows; no spline scan or allocations per anchor.
 */
[[nodiscard]] Result<SplineFrameSample> sampleSurfacePolyline(const RoadSurfacePolyline& path, float distance);
/** @brief Closest owned center sample for collision checks, linear in retained road rows. */
[[nodiscard]] Result<SplineSample> closestSurfacePolyline(const RoadSurfacePolyline& path, float x, float y, float z);
/** @brief Bounded height-only projection; provider and options are borrowed synchronously, no callbacks retained. */
[[nodiscard]] Result<float> sideTerrainHeight(const RoadSideTerrainRule& rule, eve::IProcgenWorldQuery& query, float x,
                                              float z, const ProcgenPoint& pose,
                                              const std::array<float, 6>* assetBounds, const RoadBakeOptions& options,
                                              std::size_t& work);
/** @brief Module-private bounded sampling; zero metric spacing retains explicit fixed interval counts. */
Result<std::vector<SplineFrameSample>> sampleRoadFrames(const SplinePath& path, const RoadEdge& edge,
                                                        const RoadBakeOptions& options, float length,
                                                        std::size_t& samplingWork);
/** @brief Module-private bounded parameter scan; pose construction remains owned by SplinePath. */
Result<std::vector<SplineFrameSample>> sampleRoadParametricFrames(const SplinePath& path, const RoadEdge& edge,
                                                                  const RoadBakeOptions& options,
                                                                  std::size_t&           samplingWork);
/** @brief Clip source triangles into frame slabs and deform their positions and inverse-transpose normals. */
[[nodiscard]] Result<void> bakeFacilityPrefab(MeshBuild& mesh, const RoadFacilityPrefab& prefab,
                                              const std::vector<SplineFrameSample>& frames, float sourceLow,
                                              float sourceHigh, float fullSourceLength, std::size_t budget);
struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

/** @brief Sample a profile-side offset from actual retained row vectors and widths, not a reconstructed normal.
 * @cost Logarithmic row lookup; endpoint offsets clamp while the center extrapolates.
 */
[[nodiscard]] Result<V3> sampleSurfaceProfilePoint(const RoadSurfacePolyline& path, float distance, float side,
                                                   float asphaltHalf);

struct JunctionPlane {
    V3    origin;
    float gradeX = 0.f;
    float gradeZ = 0.f;
    V3    up{0.f, 1.f, 0.f};
};

/** @brief Module-private transverse width transform preserving fixed-width shoulders. */
float scaledProfileSide(float side, float asphaltHalf, float scale);
/** @brief Module-private driving-surface half-width from a generated profile. */
float profileAsphaltHalfWidth(const RoadProfile& profile);
/** @brief Module-private conservative half-width bound for varying-width junction sockets. */
float maximumAsphaltHalfWidth(const RoadEdge& edge);

template <class T>
Result<T> bakeFail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.road"));
}

/** @brief Module-private road bake implementation helper. */
V3 operator+(V3 a, V3 b);

/** @brief Module-private road bake implementation helper. */
V3 operator-(V3 a, V3 b);

/** @brief Module-private road bake implementation helper. */
V3 operator*(V3 a, float s);

/** @brief Module-private road bake implementation helper. */
float dot(V3 a, V3 b);

/** @brief Module-private road bake implementation helper. */
V3 cross(V3 a, V3 b);

/** @brief Module-private road bake implementation helper. */
float length(V3 a);

/** @brief Module-private road bake implementation helper. */
V3 normalize(V3 a);
/** @brief Borrow the center surface normal with the same network-wide projection budget; no retained provider. */
[[nodiscard]] Result<V3> sideTerrainNormal(eve::IProcgenWorldQuery& query, float x, float z,
                                           const RoadBakeOptions& options, std::size_t& work);

/** @brief Module-private road bake implementation helper. */
bool isMeshBudgetExceeded(const MeshBuild& mesh, const RoadBakeOptions& options);

/** @brief Module-private road bake implementation helper. */
float junctionTrimDistance(float pathLength, float requestedRadius);

/** @brief Module-private road bake implementation helper. */
int circularArcSegments(float radius, float angle, float maximumError);

/** @brief Module-private road bake implementation helper. */
int quadraticSegments(V3 start, V3 control, V3 end, float angle, float maximumError);

/** @brief Module-private road bake implementation helper. */
Result<SplinePath> edgeToSpline(const RoadEdge& edge);

/** @brief Module-private road bake implementation helper. */
V3 authoredOutwardDirection(const RoadNode& node, const RoadEdge& edge);

/** @brief Module-private road bake implementation helper. */
float asphaltHalfWidth(const RoadEdge& edge);

/** @brief Module-private road bake implementation helper. */
float junctionSocketDistance(const RoadNetwork& network, const RoadNode& node, const RoadEdge& edge, float pathLength);

/** @brief Module-private road bake implementation helper. */
float planeHeight(const JunctionPlane& plane, float x, float z);

/** @brief Module-private road bake implementation helper. */
JunctionPlane fitJunctionPlane(const RoadNetwork& network, const RoadNode& node);

/** @brief Module-private road bake implementation helper. */
Result<void> validateJunctionGeometry(const RoadNetwork& network, const RoadNode& node);

/** @brief Module-private road bake implementation helper. */
void blendFrameToJunctionPlane(SplineFrameSample& frame, const JunctionPlane& plane, float weight);

/** @brief Module-private road bake implementation helper. */
float signedAreaXZ(const std::vector<V3>& polygon);

/** @brief Module-private road bake implementation helper. */
float crossXZ(V3 a, V3 b, V3 c);

/** @brief Module-private road bake implementation helper. */
bool isPointInTriangleXZ(V3 p, V3 a, V3 b, V3 c, float orientation);

/** @brief Module-private road bake implementation helper. */
enum class TriangulationStatus { Complete, Degenerate };
enum class BoundaryPointPolicy { IgnoreCollinear, IncludeBoundary };
[[nodiscard]] TriangulationStatus triangulateBoundary(const std::vector<V3>&                   polygon,
                                                      std::vector<std::array<std::size_t, 3>>& triangles,
                                                      BoundaryPointPolicy                      boundaryPolicy);

/** @brief Module-private road bake implementation helper. */
float laneCenterOffset(const RoadStyle& style, int lanesForward, int lanesBackward, int laneIndex,
                       RoadLaneDirection direction);

/** @brief Module-private road bake implementation helper. */
void appendBox(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float hx, float hy, float hz,
               RoadMaterial material);

/** @brief Module-private road bake implementation helper. */
enum class QuadDiagonal { FirstToThird, SecondToFourth };
/** @brief Emit a bounded strip quad with the selected physical diagonal and unchanged vertex attributes. */
void appendStripQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                     RoadMaterial material, QuadDiagonal diagonal = QuadDiagonal::FirstToThird);

/** @brief Module-private road bake implementation helper. */
void appendOrientedQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                        RoadMaterial material, QuadDiagonal diagonal = QuadDiagonal::FirstToThird);

/** @brief Module-private road bake implementation helper. */
void appendOrientedTri(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 normal, float uvMeters, RoadMaterial material);

/** @brief Module-private road bake implementation helper. */
Result<void> loftProfileClean(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadProfile& profile,
                              float pathLength, float uvMeters, RoadMaterial asphaltMaterial, std::size_t budget,
                              int subdivisions = 1);

/** @brief Module-private road bake implementation helper. */
Result<void> capProfileRing(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward);

/** @brief Module-private road bake implementation helper. */
void capProfileShoulders(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward);

/** @brief Module-private road bake implementation helper. */
Result<void> addDeckAndPiers(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadStyle& style,
                             float halfWidth, float asphaltHalf, bool includePiers, std::size_t budget);

/** @brief Module-private road bake implementation helper. */
Result<void> addLaneMarkings(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadEdge& edge,
                             float pathLength, const RoadBakeOptions& options);

/** @brief Module-private road bake implementation helper. */
void appendArrow(MeshBuild& mesh, V3 pos, V3 forward, V3 up, float size);

/** @brief Module-private road bake implementation helper. */
Result<void> bakeStructuralGeometry(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadEdge& edge,
                                    const RoadProfile& profile, float pathLength, const RoadBakeOptions& options);
/** @brief Sweep continuous facilities and clip them to exact interval/socket boundaries. */
[[nodiscard]] Result<void> bakeContinuousFacilities(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames,
                                                    const RoadNetwork& network, const RoadEdge& edge, float pathLength,
                                                    const RoadBakeOptions&                   options,
                                                    const std::unordered_set<std::uint32_t>& activeNodes);
/** @brief Bake one edge. */
Result<void> bakeEdgeGeometry(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network, const RoadEdge& edge,
                              const RoadBakeOptions&                   options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes,
                              const std::unordered_set<std::uint32_t>& terminalNodes, RoadFrameCache& frameCache);

/** @brief Module-private road bake implementation helper. */
Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                          const RoadBakeOptions& options);

/** @brief Module-private road bake implementation helper. */
Result<void> bakeTurnOverlays(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network,
                              const RoadBakeOptions&                   options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes);

/** @brief Module-private road bake implementation helper. */
Result<void> bakePlacementPoints(PointSet& placements, const RoadNetwork& network, const RoadBakeOptions& options,
                                 const std::unordered_set<std::uint32_t>& activeJunctionNodes,
                                 RoadFrameCache&                          frameCache);


/** @brief Query compatible degree-two facility continuation; no retained peer pointer. */
bool isFacilityContinued(const RoadNetwork& network, std::uint32_t nodeId, const RoadEdge& edge,
                         const RoadContinuousFacility& facility);
/** @brief Fill compatible continuous-facility gaps across active degree-two junction sockets. */
[[nodiscard]] Result<void> bakeFacilityContinuations(MeshBuild& mesh, const RoadNetwork& network,
                                                     const RoadBakeOptions&                   options,
                                                     const std::unordered_set<std::uint32_t>& activeNodes);

[[nodiscard]] Result<SplinePath> edgeSplineForBake(const RoadEdge& edge);
[[nodiscard]] float              junctionSocketDistanceForBake(const RoadNetwork& network, const RoadNode& node,
                                                               const RoadEdge& edge, float pathLength);
[[nodiscard]] Result<void>       bakeTrafficControlPoints(PointSet& placements, const RoadNetwork& network,
                                                          const RoadBakeOptions& options);

}  // namespace eve::procgen::road::detail
