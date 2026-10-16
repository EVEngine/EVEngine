#pragma once

#include <optional>
#include "common/PrefabRootPose.h"
#include "common/ResourceRef.h"
#include "common/Result.h"
#include "procgen/road/RoadTerrainPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen::road {

/** @brief Outgoing road span controller; StraightXZ retains smooth height, StraightXYZ is fully linear. */
enum class RoadPathControl : std::uint8_t { Spline = 0, StraightXZ = 1, StraightXYZ = 2, Circular = 3 };

/** @brief One authored control point on a road centerline (world space, Y-up). */
struct RoadControlPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float widthScale =
        1.f;  ///< Positive driving-surface width multiplier in [0.05,16]; curb/sidewalk widths stay in metres.
    float rollDegrees =
        0.f;  ///< Unwrapped crossfall in [-360,360] degrees; interpolation preserves the authored rotation direction.
    float splineStrength = .5f;  ///< Outgoing Catmull span tangent strength in [0.01,1]; .5 preserves classic Catmull.
    RoadPathControl pathControl        = RoadPathControl::Spline;  ///< Controller of this marker to the next marker.
    bool            alignStraightStart = true,
         alignStraightEnd =
             false;  ///< Straight-adjacent XZ alignment with zero vertical derivative; reversal swaps ends.
    bool  circularFromEnd  = false;  ///< Derive circular geometry from the following endpoint tangent after reversal.
    float rollCenterOffset = 0.f;    ///< Signed width fraction of the roll axis: -.5 left, 0 center, .5 right.
};

/** @brief Profile material tags used when lofting a road cross-section. */
enum class RoadMaterial : std::uint8_t {
    Asphalt = 0,
    Curb,
    Sidewalk,
    Deck,
    Pier,
    Marking,
    MarkingYellow,
    Nav,
    BridgeAsphalt,
    TunnelAsphalt,
    Tunnel,
    TunnelPortalAsphalt,  ///< Driving surface inside an authored tunnel portal footprint.
};

/** @brief One 2D profile sample in the edge local (side, up) plane. */
struct RoadProfilePoint {
    float        side     = 0.f;
    float        up       = 0.f;
    RoadMaterial material = RoadMaterial::Asphalt;
};

/** @brief Explicit structural treatment of a bounded road interval. */
enum class RoadStructureKind : std::uint8_t { Bridge = 0, Tunnel = 1 };

/** @brief Normalized arc-distance interval; nonoverlapping, sorted, owned by RoadNetwork. */
struct RoadStructureInterval {
    float             start = 0.f, end = 1.f;
    RoadStructureKind kind          = RoadStructureKind::Bridge;
    float             tunnelHeight  = 4.5f;                ///< Interior roof height above the centerline.
    float             wallThickness = 0.35f;               ///< Positive tunnel wall/roof thickness in metres.
    float portalStartLength = 2.f, portalEndLength = 2.f;  ///< Hole footprint depths in metres; zero omits a portal.
};

/** @brief Owned source vertex in prefab-local (side, up, forward) axes, with normal and UV. */
struct RoadPrefabVertex {
    float x = 0.f, y = 0.f, z = 0.f;
    float nx = 0.f, ny = 1.f, nz = 0.f;
    float u = 0.f, v = 0.f;
};
/** @brief Indexed source-prefab triangle with renderer-neutral material. */
struct RoadPrefabTriangle {
    std::uint32_t a = 0, b = 0, c = 0;
    RoadMaterial  material = RoadMaterial::Curb;
    std::uint32_t smoothingGroup =
        0;  ///< Zero preserves authored normals; equal positive groups smooth coincident positions.
};
/** @brief Optional owned source segment for a prefab's start or end. Empty mesh omits the segment. */
struct RoadPrefabSegment {
    std::string                     assetId;
    float                           length = 1.f;
    std::vector<RoadPrefabVertex>   vertices;
    std::vector<RoadPrefabTriangle> triangles;
};
/** @brief Owned prefab mesh; local Z in [0,length] deforms along the facility arc.
 * Source range preserves cut geometry on splits. UVs retain the authored source values.
 */
struct RoadFacilityPrefab {
    RoadPrefabSegment startMesh, endMesh;
    int  middleCopies = 1;  ///< Bounded authored repetitions; segment lengths determine their relative arc lengths.
    bool weldCompatibleVertices = false;  ///< Share indices only when position, normal and UV agree within 1e-5.
    bool automaticSpacing =
        false;  ///< Fit the nearest positive number of middle segments, averaging spacing over the arc.
    bool averageSpacing = true;  ///< Automatic mode distributes middle segments to the end; false clips the final
                                 ///< segment at exact source length.
    bool reverseRepeatSequence =
        false;  ///< Retained placement orientation for exact-spacing partial segments after reversal.
    float repeatSourceLength =
        0.f;  ///< Zero derives the full source arc; split fragments retain the original metre span.
    std::string                     assetId;
    float                           length = 1.f, sourceStart = 0.f, sourceEnd = 1.f;
    std::vector<RoadPrefabVertex>   vertices;
    std::vector<RoadPrefabTriangle> triangles;
};
/** @brief Continuous facility over an authored edge interval; all profiles and prefab meshes are owned values. */
struct RoadContinuousFacility {
    RoadFacilityPrefab prefab;     ///< Empty source mesh selects section sweep; profile remains the socket envelope.
    std::uint32_t      id    = 1;  ///< Unique within an edge style; preserved by split and reversal.
    float              start = 0.f, end = 1.f;
    float              lateralOffset  = 0.f;  ///< Metres from the authored centerline, signed along its side axis.
    float              verticalOffset = 0.f;
    std::vector<RoadProfilePoint> profile;  ///< Closed simple section; outgoing segment material.
    bool                          capStart = true, capEnd = true;
    float                         uvMeters = 2.f;
};

/** @brief Height-only ground projection of generic side-object anchors; bounds are local horizontal footprint metres.
 */
enum class RoadSideTerrainMode : std::uint8_t { None = 0, Center = 1, Lowest = 2, Highest = 3, Average = 4 };
/** @brief Owned side-instance orientation rule; height selection remains independently authored. */
enum class RoadSideAlignment : std::uint8_t { None = 0, TerrainNormal = 1, PathForward = 2, PathNormal = 3 };
/** @brief Stable asset link at an exact admitted package build; stale resolution fails without substituting newer data.
 */
struct RoadSideAssetBinding {
    eve::AssetRef       asset;
    eve::PersistentId   buildId;
    eve::PrefabRootPose rootPose =
        eve::PrefabRootPose::ReplaceTranslationRotation;  ///< New road placements replace the source root pose.
};
/** @brief Owned side-object ground rule; None height retains road height, while TerrainNormal alignment independently
 * queries ground. */
struct RoadSideTerrainRule {
    RoadSideTerrainMode mode    = RoadSideTerrainMode::None;
    float               minSide = -.5f, maxSide = .5f, minForward = -.5f, maxForward = .5f;
    int                 samplesPerAxis = 3;  ///< Uniform inclusive footprint grid in [2,17]; Center performs one query.
    RoadSideAlignment   alignment =
        RoadSideAlignment::None;  ///< None keeps yaw only; other modes produce full Euler orientation.
    std::optional<RoadSideAssetBinding>
        asset{};  ///< Absent uses explicit footprint; present resolves admitted prefab bounds.
};

/** @brief Cross-section and structural style for one road edge. */
enum class RoadWidthInterpolation : std::uint8_t { Linear = 0, CatmullRom = 1 };
/** @brief Cross-section orientation before authored roll; snapshots missing this field retain transport. */
enum class RoadFrameOrientation : std::uint8_t { ParallelTransport = 0, WorldUp = 1 };
/** @brief Crossfall interpolation between authored markers. */
enum class RoadRollInterpolation : std::uint8_t { Linear = 0, SmoothStep = 1 };
/** @brief Rigid side-anchor distribution; historical snapshots retain the half-spacing phase.
 * Fixed starts at the clipped span start and excludes its end. Average fits a nearest-even,
 * minimum-one interval count and adds the measured .005 metre interval bias.
 */
enum class RoadSideDistribution : std::uint8_t { LegacyCentered = 0, Fixed = 1, Average = 2 };
/** @brief Lateral origin for rigid facilities; LegacyOutside retains historical bake-option offsets. */
enum class RoadSideReference : std::uint8_t { LegacyOutside = 0, AsphaltEdges = 1, Center = 2 };
/** @brief Facility path policy; SurfaceSegments follows the road's retained section centers and segment directions. */
enum class RoadSidePathSampling : std::uint8_t { Analytic = 0, SurfaceSegments = 1 };
/** @brief Owning rigid-facility placement settings, copied with the road style.
 * Explicit references ignore bake sideObjectOffset. Lateral offset is signed path-right metres,
 * applied after width scaling; vertical offset is world-up metres after terrain selection.
 * Yaw is world-Y degrees added to the resolved orientation before footprint queries.
 * Center emits one stream if either side is enabled, preferring the left role when both are enabled.
 * Missing snapshot settings retain LegacyOutside and zero offsets. No external lifetime or threading state.
 */
struct RoadSidePlacement {
    RoadSideReference    reference      = RoadSideReference::LegacyOutside;
    float                lateralOffset  = 0.f;  ///< Signed path-right metres, bounded to +/-10000.
    float                verticalOffset = 0.f;  ///< World-up metres, bounded to +/-10000; reference default is .02.
    float                yawDegrees     = 0.f;  ///< Additional world-Y rotation, bounded to +/-360.
    RoadSidePathSampling pathSampling =
        RoadSidePathSampling::Analytic;  ///< Explicit sampled-surface opt-in; missing fields retain Analytic.
};
/** @brief Owning metric sampling semantics; absent snapshot fields retain MaximumIntervals. */
enum class RoadSamplingPolicy : std::uint8_t { MaximumIntervals = 0, ParametricDistanceTurn = 1 };

/** @brief Owning cross-section, width policy and structural authoring for one road edge. */
struct RoadStyle {
    RoadSidePlacement     sidePlacement;  ///< Single source for rigid-facility origin and transform offsets.
    RoadSideTerrainRule   sideTerrain;    ///< Terrain height selection for side.left/right anchors only.
    RoadRollInterpolation rollInterpolation =
        RoadRollInterpolation::SmoothStep;  ///< New roads ease crossfall per spline span; old snapshots retain Linear.
    RoadFrameOrientation frameOrientation =
        RoadFrameOrientation::WorldUp;  ///< New roads retain horizontal unbanked sections.
    RoadWidthInterpolation widthInterpolation =
        RoadWidthInterpolation::CatmullRom;      ///< New roads match reference cubic width; old snapshots migrate to
                                                 ///< Linear.
    std::vector<RoadProfilePoint> crossSection;  ///< Empty selects the standard road; custom segment tags are explicit.
    bool                          crossSectionClosed = false;  ///< Close the final-to-first section segment.
    bool automaticStructures = true;  ///< Empty intervals retain legacy decks/piers unless explicitly disabled.
    std::vector<RoadStructureInterval>
        structures;  ///< Explicit normalized arc intervals; nonempty disables legacy automatic structures.
    std::vector<RoadContinuousFacility> facilities;  ///< Continuous swept facilities, clipped by sockets.

    float laneWidth      = 3.5f;
    float curbWidth      = 0.45f;   ///< Jersey-barrier thickness.
    float curbHeight     = 0.55f;   ///< Raised barrier height (readable asphalt channel).
    float sidewalkWidth  = 1.5f;
    float sidewalkHeight = 0.14f;
    float deckThickness  = 0.70f;
    float pierWidth      = 1.35f;
    float pierDepth      = 1.35f;
    float pierSpacing    = 11.f;
    float pierClearance  = 1.5f;
    float markingWidth   = 0.22f;
    float dashLength     = 3.5f;
    float dashGap        = 3.f;
    float uvMeters       = 8.f;  ///< World-space metres per texture repeat.
    float speedLimitMps  = 13.8889f;  ///< Navigation speed limit in metres per second.
    int   trafficPriority = 0;        ///< Higher incoming-road values win uncontrolled junction priority.
    float sideObjectStartOffset = 0.f;  ///< Empty distance before the first side-object anchor.
    RoadSideDistribution sideObjectDistribution =
        RoadSideDistribution::Average;  ///< Explicit rigid-anchor spacing policy.
    float sideObjectEndOffset   = 0.f;  ///< Empty distance after the last side-object anchor.
    bool  sideObjectsLeft       = true;  ///< Emit anchors on the authored centerline's left side.
    bool  sideObjectsRight      = true;  ///< Emit anchors on the authored centerline's right side.
    float samplingSpacingMeters =
        2.f;  ///< New roads use the measured 2m trigger; explicit zero keeps legacy fixed counts.
    float samplingAngleDegrees     = 45.f;  ///< New roads match the reference 45 degree target; scan requires >=1.
    int   crossSectionSubdivisions = 1;     ///< Subdivide every authored section segment, bounded to [1,32].
    RoadSamplingPolicy samplingPolicy =
        RoadSamplingPolicy::ParametricDistanceTurn;  ///< New roads scan metric distance/turn; legacy snapshots retain
                                                     ///< their policy.
    RoadTerrainPolicy terrainPolicy;  ///< Authoritative ground-channel policy; execution remains explicit.
};

/** @brief Authoritative traffic-control policy applied to every approach of one junction node. */
enum class RoadJunctionControl : std::uint8_t {
    Uncontrolled = 0,  ///< Edge trafficPriority resolves right-of-way without a mandatory stop.
    Yield,             ///< Lower-priority approaches yield; equal-priority approaches all yield.
    Stop,              ///< Every incoming approach receives a stop control.
    Signal,            ///< Every incoming approach is controlled by a traffic signal.
};

/** @brief Owning renderer-neutral custom connection vertex in node-local space. */
struct RoadConnectionVertex {
    float x = 0.f, y = 0.f, z = 0.f;
    float nx = 0.f, ny = 1.f, nz = 0.f;
    float u = 0.f, v = 0.f;
};
/** @brief Material-qualified indexed triangle owned by a custom connection. */
struct RoadConnectionTriangle {
    std::uint32_t a = 0, b = 0, c = 0;
    RoadMaterial  material = RoadMaterial::Asphalt;
};
/** @brief Asset port with outward orthonormal frame, full seam section and network-local edge binding. */
struct RoadConnectionPort {
    std::uint32_t id = 1, edgeId = 0;  ///< Zero edgeId leaves an unused asset port unbound.
    float         x = 0.f, y = 0.f, z = 0.f;
    float         forwardX = 1.f, forwardY = 0.f, forwardZ = 0.f;
    float         upX = 0.f, upY = 1.f, upZ = 0.f;
    bool          sectionClosed = false;    ///< Closed seam topology must match the attached road profile.
    std::vector<RoadProfilePoint> section;  ///< Outward-facing section; must coincide with mesh boundary vertices.
};
/** @brief One explicit undirected facility route through a node; endpoints belong to incident edges.
 * Each endpoint can occur in one route only. Network owns and validates these identities.
 */
struct RoadFacilityRoute {
    std::uint32_t firstEdge = 0, firstFacility = 0, secondEdge = 0, secondFacility = 0;
};
/** @brief Owning custom junction asset snapshot; all geometry/ports share one node-local transform. */
struct RoadConnection {
    std::vector<RoadFacilityRoute>      facilityRoutes;  ///< Explicit routes override automatic two-arm continuation.
    std::string                         assetId;
    float                               yawDegrees = 0.f;
    std::vector<RoadConnectionVertex>   vertices;
    std::vector<RoadConnectionTriangle> triangles;
    std::vector<RoadConnectionPort>     ports;  ///< Empty selects the procedural junction, with no custom mesh.
};
/** @brief Junction node owned by a road network. */
struct RoadNode {
    std::uint32_t id             = 0;
    float         x              = 0.f;
    float         y              = 0.f;
    float         z              = 0.f;
    float         junctionRadius = 6.f;
    RoadJunctionControl junctionControl = RoadJunctionControl::Uncontrolled;
    RoadConnection      connection;
};

/** @brief Directed centerline edge with optional reverse lanes. */
struct RoadEdge {
    std::uint32_t                id            = 0;
    std::uint32_t                from          = 0;
    std::uint32_t                to            = 0;
    std::vector<RoadControlPoint> controlPoints;
    int                          lanesForward  = 2;
    int                          lanesBackward = 0;
    RoadStyle                    style;
};

/** @brief Stable identities produced by one atomic edge split. */
struct RoadEdgeSplitResult {
    std::uint32_t nodeId       = 0;
    std::uint32_t firstEdgeId  = 0;  ///< Original edge identity retained by the first half.
    std::uint32_t secondEdgeId = 0;  ///< Newly allocated continuation edge.
};

/** @brief Travel direction of a lane relative to the authored edge centerline. */
enum class RoadLaneDirection : std::uint8_t {
    Forward = 0,  ///< Travels from RoadEdge::from to RoadEdge::to.
    Backward,     ///< Travels from RoadEdge::to to RoadEdge::from.
};

/**
 * @brief One legal lane-to-lane connection inside a junction.
 *
 * Value type owned by RoadNetwork (not a cross-domain Link projection). Edge and
 * lane indices stay valid until the network mutates and bumps its revision.
 */
struct RoadLaneConnection {
    std::uint32_t inEdge   = 0;
    int           inLane   = 0;
    std::uint32_t outEdge  = 0;
    int           outLane  = 0;
    RoadLaneDirection inDirection  = RoadLaneDirection::Forward;
    RoadLaneDirection outDirection = RoadLaneDirection::Forward;
};

/** @brief One renderer-neutral polyline chunk for navigation / marking overlays. */
struct RoadPolyline {
    std::vector<float> xyz;  ///< Packed xyz samples.
    float              r = 0.2f, g = 0.85f, b = 1.f, a = 1.f;
    float              width = 0.08f;
    bool               closed = false;
    std::uint32_t      inEdge = 0;
    std::uint32_t      outEdge = 0;
    int                inLane = -1;
    int                outLane = -1;
    RoadLaneDirection  inDirection = RoadLaneDirection::Forward;
    RoadLaneDirection  outDirection = RoadLaneDirection::Forward;
    float              speedLimitMps = 0.f;
    int                trafficPriority = 0;
};

/** @brief Owning bake-time overlay snapshot (no pointers into the network). */
struct RoadOverlay {
    std::vector<RoadPolyline> lanes;
    std::vector<RoadPolyline> turns;
};

/** @brief Owning open profile polyline for one edge style and lane layout. */
struct RoadProfile {
    std::vector<RoadProfilePoint> points;
    float                         halfWidth = 0.f;
    bool                          closed            = false;
    bool                          explicitMaterials = false;
};

/**
 * @brief Build a mathematical road cross-section for the given lane counts.
 * @param style Geometric style; widths must be finite and non-negative.
 * @param lanesForward Forward driving lanes (>= 0).
 * @param lanesBackward Optional opposite lanes (>= 0).
 * @note At least one direction must contain a lane.
 * @return Owning profile, or a structured validation diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadProfile> makeRoadProfile(const RoadStyle& style, int lanesForward,
                                                                       int lanesBackward);

/**
 * @brief Map a material tag to the MeshBuild group name used by the baker.
 * @ownership borrowed — returns a pointer to a static string literal; callers must not free it.
 * @lifetime Program lifetime; the pointer remains valid for the process.
 */
[[nodiscard]] EVENGINE_API_DOMAINS const char* roadMaterialGroup(RoadMaterial material) noexcept;

}  // namespace eve::procgen::road
