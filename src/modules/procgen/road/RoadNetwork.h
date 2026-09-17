#pragma once

#include "procgen/road/RoadTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::procgen::road {

/**
 * @brief Owning directed road graph with lane connectivity.
 *
 * Mutations are synchronous and owner-thread only. Revision bumps on every
 * successful structural change. Handles/ids are stable until erase.
 */
class RoadNetwork {
public:
    /** @brief Insert a junction node after validating finite coordinates. */
    [[nodiscard]] Result<std::uint32_t> addNode(float x, float y, float z, float junctionRadius = 6.f);
    /** @brief Insert a directed edge with at least two control points. */
    [[nodiscard]] Result<std::uint32_t> addEdge(std::uint32_t from, std::uint32_t to,
                                                std::vector<RoadControlPoint> controlPoints, int lanesForward = 2,
                                                int lanesBackward = 0, RoadStyle style = {});
    /** @brief Register one legal turn inside a shared junction node. */
    [[nodiscard]] Result<void> addLaneLink(RoadLaneLink link);
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
    [[nodiscard]] const std::vector<RoadLaneLink>& laneLinks() const noexcept { return laneLinks_; }

    /** @brief Build a multi-level interchange demo graph (ground cross + elevated loop + ramps). */
    [[nodiscard]] static Result<RoadNetwork> makeInterchange(float span = 48.f, float bridgeHeight = 8.f,
                                                             int lanes = 2, std::uint32_t seed = 1);

private:
    [[nodiscard]] Result<void> validateStyle(const RoadStyle& style) const;
    [[nodiscard]] int findNodeIndex(std::uint32_t id) const;
    [[nodiscard]] int findEdgeIndex(std::uint32_t id) const;

    std::vector<RoadNode>     nodes_;
    std::vector<RoadEdge>     edges_;
    std::vector<RoadLaneLink> laneLinks_;
    std::unordered_map<std::uint32_t, int> nodeIndex_;
    std::unordered_map<std::uint32_t, int> edgeIndex_;
    std::uint32_t nextNodeId_ = 1;
    std::uint32_t nextEdgeId_ = 1;
    std::uint64_t revision_   = 0;
};

}  // namespace eve::procgen::road
