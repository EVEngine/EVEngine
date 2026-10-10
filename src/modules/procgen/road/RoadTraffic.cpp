#include "procgen/road/RoadBakeInternal.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eve::procgen::road::detail {
namespace {

struct Approach {
    const RoadEdge* edge = nullptr;
    const RoadNode* node = nullptr;
    SplinePath spline;
    float      length = 0.f;
    float      socketDistance = 0.f;
    float      halfWidth = 0.f;
    RoadLaneDirection direction = RoadLaneDirection::Forward;
};

const char* controlRole(RoadJunctionControl control) {
    switch (control) {
        case RoadJunctionControl::Yield: return "junction.control.yield";
        case RoadJunctionControl::Stop: return "junction.control.stop";
        case RoadJunctionControl::Signal: return "junction.control.signal";
        case RoadJunctionControl::Uncontrolled: break;
    }
    return "junction.control.uncontrolled";
}

}  // namespace

Result<void> bakeTrafficControlPoints(PointSet& placements, const RoadNetwork& network,
                                      const RoadBakeOptions& options) {
    std::vector<Approach> approaches;
    for (const auto& node : network.nodes()) {
        if (node.junctionControl == RoadJunctionControl::Uncontrolled) continue;
        const auto incidentCount = std::count_if(network.edges().begin(), network.edges().end(),
                                                 [&](const RoadEdge& edge) {
                                                     return edge.from == node.id || edge.to == node.id;
                                                 });
        if (incidentCount < 2) continue;

        int maximumPriority = 0;
        bool hasIncidentEdge = false;
        for (const auto& edge : network.edges()) {
            if (edge.from != node.id && edge.to != node.id) continue;
            maximumPriority = hasIncidentEdge ? std::max(maximumPriority, edge.style.trafficPriority)
                                              : edge.style.trafficPriority;
            hasIncidentEdge = true;
        }

        bool hasLowerPriority = false;
        for (const auto& edge : network.edges())
            if ((edge.from == node.id || edge.to == node.id) && edge.style.trafficPriority < maximumPriority)
                hasLowerPriority = true;

        for (const auto& edge : network.edges()) {
            RoadLaneDirection direction;
            if (edge.to == node.id && edge.lanesForward > 0)
                direction = RoadLaneDirection::Forward;
            else if (edge.from == node.id && edge.lanesBackward > 0)
                direction = RoadLaneDirection::Backward;
            else
                continue;
            if (node.junctionControl == RoadJunctionControl::Yield && hasLowerPriority &&
                edge.style.trafficPriority == maximumPriority)
                continue;

            auto spline = edgeSplineForBake(edge);
            if (!spline.ok()) return Result<void>::failure(spline.status());
            auto length = spline.value().lengthResult(32);
            if (!length.ok()) return Result<void>::failure(length.status());
            auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
            if (!profile.ok()) return Result<void>::failure(profile.status());
            approaches.push_back({&edge, &node, std::move(spline).takeValue(), length.value(),
                                  junctionSocketDistanceForBake(network, node, edge, length.value()),
                                  profile.value().halfWidth, direction});
        }
    }

    if (static_cast<std::size_t>(placements.getCount()) + approaches.size() >
        static_cast<std::size_t>(options.maximumPlacements))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "road traffic-control anchors exceed the placement budget",
                                                       "placements", {}, "procgen.road"));

    for (const auto& approach : approaches) {
        const bool forward = approach.direction == RoadLaneDirection::Forward;
        const float distance = forward ? approach.length - approach.socketDistance : approach.socketDistance;
        auto frame = approach.spline.travelFrameResult(distance, "clamp", 32);
        if (!frame.ok()) return Result<void>::failure(frame.status());
        const auto& f = frame.value();
        const float directionSign = forward ? 1.f : -1.f;
        const float lateral = (forward ? 1.f : -1.f) * (approach.halfWidth + options.sideObjectOffset);
        const float dx = f.forwardX * directionSign;
        const float dy = f.forwardY * directionSign;
        const float dz = f.forwardZ * directionSign;
        const int row = placements.add(f.sample.x + f.sideX * lateral, f.sample.y + f.sideY * lateral,
                                       f.sample.z + f.sideZ * lateral);
        placements.setNormal(row, f.upX, f.upY, f.upZ);
        placements.setYaw(row, std::atan2(dx, dz) * 57.2957795f);
        const char* role = controlRole(approach.node->junctionControl);
        placements.setPointSeed(row, deriveSeed(approach.node->id,
                                                std::string(role) + std::to_string(approach.edge->id)));
        const std::uint64_t ordinal = static_cast<std::uint64_t>(approach.edge->id) * 2u +
                                      static_cast<std::uint64_t>(approach.direction) + 1u;
        auto status = placements.trySetPointId(
            row, derivePointId(0x4a43545200000000ull | approach.node->id, ordinal));
        if (!status.ok()) return status;
        status = placements.trySetStringAttribute(row, "road_role", role);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_node_id", approach.node->id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_edge_id", approach.edge->id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_side", 1);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "traffic_priority", approach.edge->style.trafficPriority);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_lane_direction", static_cast<int>(approach.direction));
        if (!status.ok()) return status;
        status = placements.trySetFloatAttribute(row, "road_distance", distance);
        if (!status.ok()) return status;
        status = placements.trySetVectorAttribute(row, "road_direction", dx, dy, dz);
        if (!status.ok()) return status;
    }
    return Result<void>::success();
}

}  // namespace eve::procgen::road::detail
