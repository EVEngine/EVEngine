#pragma once

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen::road {

/** @brief One authored control point on a road centerline (world space, Y-up). */
struct RoadControlPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Cross-section and structural style for one road edge. */
struct RoadStyle {
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
    float sideObjectEndOffset   = 0.f;  ///< Empty distance after the last side-object anchor.
    bool  sideObjectsLeft       = true;  ///< Emit anchors on the authored centerline's left side.
    bool  sideObjectsRight      = true;  ///< Emit anchors on the authored centerline's right side.
};

/** @brief Authoritative traffic-control policy applied to every approach of one junction node. */
enum class RoadJunctionControl : std::uint8_t {
    Uncontrolled = 0,  ///< Edge trafficPriority resolves right-of-way without a mandatory stop.
    Yield,             ///< Lower-priority approaches yield; equal-priority approaches all yield.
    Stop,              ///< Every incoming approach receives a stop control.
    Signal,            ///< Every incoming approach is controlled by a traffic signal.
};

/** @brief Junction node owned by a road network. */
struct RoadNode {
    std::uint32_t id             = 0;
    float         x              = 0.f;
    float         y              = 0.f;
    float         z              = 0.f;
    float         junctionRadius = 6.f;
    RoadJunctionControl junctionControl = RoadJunctionControl::Uncontrolled;
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
};

/** @brief One 2D profile sample in the edge local (side, up) plane. */
struct RoadProfilePoint {
    float        side     = 0.f;
    float        up       = 0.f;
    RoadMaterial material = RoadMaterial::Asphalt;
};

/** @brief Owning open profile polyline for one edge style and lane layout. */
struct RoadProfile {
    std::vector<RoadProfilePoint> points;
    float                         halfWidth = 0.f;
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
