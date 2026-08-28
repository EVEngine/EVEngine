#pragma once

/**
 * @file ClimbingAnchorAuthoring.h
 * @brief Deterministic authoring primitives, baker, and renderer-neutral editor overlay for climbing anchors.
 */

#include "climbing/ClimbingAnchorGraph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::climbing {

/** @brief Settings whose canonical values participate in the baked graph build hash. */
struct ClimbingAnchorBakeSettings {
    float         handSpacing = 0.42f;
    float         hangingFeetDrop = 1.45f;
    float         ladderMountOffset = 0.45f;
    std::uint32_t occupancySlots = 1;
};

/** @brief Ordered body-local ledge samples supplied by an editor geometry extractor. */
struct ClimbingLedgeBakeSource {
    std::string              id;
    std::vector<Vec3>        points;
    Vec3                     localNormal{0.f, 0.f, -1.f};
    /** @brief Optional per-point wall normals; empty uses localNormal for every sample. */
    std::vector<Vec3>        localNormals;
    bool                     closed = false;
    std::vector<std::string> tags;
};

/** @brief Body-local ladder description supplied by an editor geometry extractor. */
struct ClimbingLadderBakeSource {
    std::string              id;
    Vec3                     bottomCenter;
    Vec3                     localUp{0.f, 1.f, 0.f};
    Vec3                     localNormal{0.f, 0.f, -1.f};
    float                    rungSpacing = 0.3f;
    float                    width = 0.5f;
    std::uint32_t            rungCount = 6;
    std::vector<std::string> tags;
};

/** @brief Complete owning input transaction for deterministic anchor-graph baking. */
struct ClimbingAnchorBakeRequest {
    /** @brief Canonical schema id for editor-to-baker transactions. */
    static constexpr std::string_view SchemaId = "evengine.climbing-anchor-bake-request";
    /** @brief Current editor-to-baker transaction schema version. */
    static constexpr std::int64_t SchemaVersion = 1;

    std::string                       graphId;
    std::string                       sourceGeometryContentId;
    ClimbingAnchorBakeSettings        settings;
    std::vector<ClimbingLedgeBakeSource> ledges;
    std::vector<ClimbingLadderBakeSource> ladders;
};

/** @brief Encodes an owning bake request for editor/script transport. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingAnchorBakeRequest(const ClimbingAnchorBakeRequest& request);
/** @brief Decodes all known bake request fields without publishing a graph. */
[[nodiscard]] eve::Result<ClimbingAnchorBakeRequest> decodeClimbingAnchorBakeRequest(const eve::Value& value);

/** @brief Measured bake result used by tools and build telemetry. */
struct ClimbingAnchorBakeResult {
    ClimbingAnchorGraphDefinition graph;
    std::uint32_t                  ledgeNodeCount = 0;
    std::uint32_t                  ladderNodeCount = 0;
};

/** @brief One renderer-neutral node primitive in a body-local editor overlay. */
struct ClimbingAnchorOverlayNode {
    std::string        id;
    ClimbingAnchorKind kind = ClimbingAnchorKind::Ledge;
    Vec3               position;
    Vec3               normal;
    Vec3               tangent;
    Vec3               leftHandSocket;
    Vec3               rightHandSocket;
    Vec3               feetSocket;
    std::uint32_t      occupancySlots = 0;
};

/** @brief One renderer-neutral directed edge primitive in a body-local editor overlay. */
struct ClimbingAnchorOverlayEdge {
    std::string            from;
    std::string            to;
    ClimbingAnchorEdgeKind kind = ClimbingAnchorEdgeKind::Shimmy;
    bool                   bidirectional = false;
};

/** @brief Bounded owning editor snapshot; consumers may render it without retaining graph pointers. */
struct ClimbingAnchorAuthoringOverlay {
    std::string                            graphId;
    std::string                            buildSettingsHash;
    std::vector<ClimbingAnchorOverlayNode> nodes;
    std::vector<ClimbingAnchorOverlayEdge> edges;
};

/**
 * @brief Bakes deterministic ledge and ladder topology from owning semantic geometry samples.
 * @param request Complete candidate; it is validated before any graph is published.
 * @return Canonically ordered graph and measured node counts.
 */
[[nodiscard]] eve::Result<ClimbingAnchorBakeResult> bakeClimbingAnchorGraph(
    const ClimbingAnchorBakeRequest& request);

/**
 * @brief Builds a validated owning snapshot for an editor or debug-draw adapter.
 * @param graph Complete graph candidate.
 * @return Body-local nodes and edges in canonical deterministic order.
 */
[[nodiscard]] eve::Result<ClimbingAnchorAuthoringOverlay> inspectClimbingAnchorGraphAuthoring(
    const ClimbingAnchorGraphDefinition& graph);

}  // namespace eve::climbing
