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
    float uvMeters       = 8.f;
};

/** @brief Junction node owned by a road network. */
struct RoadNode {
    std::uint32_t id             = 0;
    float         x              = 0.f;
    float         y              = 0.f;
    float         z              = 0.f;
    float         junctionRadius = 6.f;
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
};

/** @brief One renderer-neutral polyline chunk for navigation / marking overlays. */
struct RoadPolyline {
    std::vector<float> xyz;  ///< Packed xyz samples.
    float              r = 0.2f, g = 0.85f, b = 1.f, a = 1.f;
    float              width = 0.08f;
    bool               closed = false;
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
 * @param lanesForward Forward driving lanes (>= 1).
 * @param lanesBackward Optional opposite lanes (>= 0).
 * @return Owning profile, or a structured validation diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadProfile> makeRoadProfile(const RoadStyle& style, int lanesForward, int lanesBackward);

/**
 * @brief Map a material tag to the MeshBuild group name used by the baker.
 * @ownership borrowed — returns a pointer to a static string literal; callers must not free it.
 * @lifetime Program lifetime; the pointer remains valid for the process.
 */
[[nodiscard]] EVENGINE_API_DOMAINS const char* roadMaterialGroup(RoadMaterial material) noexcept;

}  // namespace eve::procgen::road
