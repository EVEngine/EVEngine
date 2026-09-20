#pragma once

#include "procgen/road/RoadTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen::road {

/**
 * @brief Owning directed road graph with lane connectivity.
 *
 * Mutations are synchronous and owner-thread only. Revision bumps on every
 * successful structural change. Handles/ids are stable until erase.
 */
class EVENGINE_API_DOMAINS RoadNetwork {
public:
    /** @brief Insert a junction node after validating finite coordinates. */
    [[nodiscard]] Result<std::uint32_t> addNode(float x, float y, float z, float junctionRadius = 6.f);
    /** @brief Insert a directed edge with at least two control points. */
    [[nodiscard]] Result<std::uint32_t> addEdge(std::uint32_t from, std::uint32_t to,
                                                std::vector<RoadControlPoint> controlPoints, int lanesForward = 2,
                                                int lanesBackward = 0, RoadStyle style = {});
    /** @brief Register one legal turn inside a shared junction node. */
    [[nodiscard]] Result<void> addLaneLink(RoadLaneConnection link);
    /** @brief Auto-connect every incoming lane to every outgoing lane at one node. */
    [[nodiscard]] Result<int> connectAllTurns(std::uint32_t nodeId);
    /** @brief Replace edge style atomically. */
    [[nodiscard]] Result<void> setEdgeStyle(std::uint32_t edgeId, RoadStyle style);
    /** @brief Clear every node, edge and link. */
    void clear();

    [[nodiscard]] int nodeCount() const noexcept { return static_cast<int>(nodes_.size()); }
    [[nodiscard]] int edgeCount() const noexcept { return static_cast<int>(edges_.size()); }
    [[nodiscard]] int laneLinkCount() const noexcept { return static_cast<int>(laneLinks_.size()); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] Result<RoadNode> nodeResult(std::uint32_t id) const;
    [[nodiscard]] Result<RoadEdge> edgeResult(std::uint32_t id) const;
    [[nodiscard]] const std::vector<RoadNode>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<RoadEdge>& edges() const noexcept { return edges_; }
    [[nodiscard]] const std::vector<RoadLaneConnection>& laneLinks() const noexcept { return laneLinks_; }

    /** @brief Build a multi-level interchange demo graph (ground cross + elevated loop + ramps). */
    [[nodiscard]] static Result<RoadNetwork> makeInterchange(float span = 48.f, float bridgeHeight = 8.f,
                                                             int lanes = 2, std::uint32_t seed = 1);

    /** @brief Scene 1: single flat straight segment (no junction). */
    [[nodiscard]] static Result<RoadNetwork> makeStraight(float length = 36.f, int lanes = 2);
    /** @brief Scene 2: gentle horizontal curve (no junction). */
    [[nodiscard]] static Result<RoadNetwork> makeCurve(float radius = 18.f, int lanes = 2);
    /** @brief Scene 3: elevated straight with piers. */
    [[nodiscard]] static Result<RoadNetwork> makeBridge(float length = 36.f, float height = 6.f, int lanes = 2);
    /** @brief Scene 4: simple ground-level 4-way cross (one junction disc). */
    [[nodiscard]] static Result<RoadNetwork> makeCross(float span = 32.f, int lanes = 2);

    /**
     * @brief Dispatch a named debug/demo scene.
     * @param scene One of: straight, curve, bridge, cross, interchange.
     */
    [[nodiscard]] static Result<RoadNetwork> makeScene(const std::string& scene, float span = 36.f,
                                                       float bridgeHeight = 6.f, int lanes = 2,
                                                       std::uint32_t seed = 1);

private:
    [[nodiscard]] Result<void> validateStyle(const RoadStyle& style) const;
    [[nodiscard]] int findNodeIndex(std::uint32_t id) const;
    [[nodiscard]] int findEdgeIndex(std::uint32_t id) const;

    std::vector<RoadNode>     nodes_;
    std::vector<RoadEdge>     edges_;
    std::vector<RoadLaneConnection> laneLinks_;
    std::unordered_map<std::uint32_t, int> nodeIndex_;
    std::unordered_map<std::uint32_t, int> edgeIndex_;
    std::uint32_t nextNodeId_ = 1;
    std::uint32_t nextEdgeId_ = 1;
    std::uint64_t revision_   = 0;
};

}  // namespace eve::procgen::road
