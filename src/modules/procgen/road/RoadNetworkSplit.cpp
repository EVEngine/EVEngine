#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/spline/SplinePath.h"

namespace eve::procgen::road {
namespace {
bool finite3(float x, float y, float z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
Result<SplinePath> makeCenterlineSpline(const RoadEdge& edge) {
    SplinePath path;
    auto       kind = path.setKindResult("catmullRom");
    if (!kind.ok()) return Result<SplinePath>::failure(kind.status());
    path.setClosed(false);
    for (const auto& point : edge.controlPoints) {
        SplinePoint splinePoint;
        splinePoint.alignStraightStart  = point.alignStraightStart;
        splinePoint.alignStraightEnd    = point.alignStraightEnd;
        splinePoint.circularFromEnd     = point.circularFromEnd;
        splinePoint.spanControl         = static_cast<eve::procgen::SplineSpanControl>(point.pathControl);
        splinePoint.spanStrength        = point.splineStrength;
        splinePoint.x                   = point.x;
        splinePoint.y                   = point.y;
        splinePoint.z                   = point.z;
        splinePoint.scaleX              = point.widthScale;
        splinePoint.scaleXInterpolation = edge.style.widthInterpolation == RoadWidthInterpolation::CatmullRom
                                              ? SplineScaleInterpolation::CatmullRom
                                              : SplineScaleInterpolation::Linear;
        splinePoint.rollDegrees         = point.rollDegrees;
        splinePoint.rollPivotSide       = -point.rollCenterOffset;
        splinePoint.worldUpFrame        = edge.style.frameOrientation == RoadFrameOrientation::WorldUp;
        splinePoint.rollInterpolation   = edge.style.rollInterpolation == RoadRollInterpolation::SmoothStep
                                              ? SplineRollInterpolation::SmoothStep
                                              : SplineRollInterpolation::Linear;
        auto added                      = path.addPointResult(splinePoint);
        if (!added.ok()) return Result<SplinePath>::failure(added.status());
    }
    return Result<SplinePath>::success(std::move(path));
}


}  // namespace
Result<RoadEdgeSplitResult> RoadNetwork::splitEdge(std::uint32_t edgeId, std::size_t controlPointIndex,
                                                   float junctionRadius) {
    return splitEdge(edgeId, controlPointIndex, junctionRadius, 0, 0);
}

Result<RoadEdgeSplitResult> RoadNetwork::splitEdgeAtSplineParameter(std::uint32_t edgeId, float parameter,
                                                                    float junctionRadius, std::uint32_t nodeId,
                                                                    std::uint32_t secondEdgeId) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0)
        return Result<RoadEdgeSplitResult>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    if (!std::isfinite(parameter) || parameter <= 0.f || parameter >= 1.f)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "spline split parameter must be inside (0,1)", "parameter"));

    const auto& original = edges_[static_cast<std::size_t>(idx)];
    auto        path     = makeCenterlineSpline(original);
    if (!path.ok()) return Result<RoadEdgeSplitResult>::failure(path.status());
    auto evaluated = path.value().evaluateResult(parameter);
    if (!evaluated.ok()) return Result<RoadEdgeSplitResult>::failure(evaluated.status());

    const int   segmentCount = path.value().segmentCount();
    const float scaled       = parameter * static_cast<float>(segmentCount);
    const int   nearest      = static_cast<int>(std::round(scaled));
    auto        candidate    = *this;
    std::size_t splitIndex   = 0;
    if (nearest > 0 && nearest < segmentCount && std::fabs(scaled - static_cast<float>(nearest)) <= 1e-5f) {
        splitIndex = static_cast<std::size_t>(nearest);
    } else {
        const int segment = std::clamp(static_cast<int>(std::floor(scaled)), 0, segmentCount - 1);
        auto      points  = original.controlPoints;
        splitIndex        = static_cast<std::size_t>(segment + 1);
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(splitIndex),
                      RoadControlPoint{evaluated.value().x, evaluated.value().y, evaluated.value().z,
                                       evaluated.value().scaleX, evaluated.value().rollDegrees,
                                       original.controlPoints[static_cast<std::size_t>(segment)].splineStrength,
                                       original.controlPoints[static_cast<std::size_t>(segment)].pathControl,
                                       original.controlPoints[static_cast<std::size_t>(segment)].alignStraightStart,
                                       original.controlPoints[static_cast<std::size_t>(segment)].alignStraightEnd,
                                       original.controlPoints[static_cast<std::size_t>(segment)].circularFromEnd,
                                       -evaluated.value().rollPivotSide});
        auto changed = candidate.setEdgeControlPoints(edgeId, std::move(points));
        if (!changed.ok()) return Result<RoadEdgeSplitResult>::failure(changed.status());
    }
    auto split = candidate.splitEdge(edgeId, splitIndex, junctionRadius, nodeId, secondEdgeId);
    if (!split.ok()) return split;
    const auto result   = split.value();
    candidate.revision_ = revision_ + 1;
    *this               = std::move(candidate);
    return Result<RoadEdgeSplitResult>::success(result);
}

Result<RoadEdgeSplitResult> RoadNetwork::splitEdge(std::uint32_t edgeId, std::size_t controlPointIndex,
                                                   float junctionRadius, std::uint32_t nodeId,
                                                   std::uint32_t secondEdgeId) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0)
        return Result<RoadEdgeSplitResult>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    const RoadEdge original = edges_[static_cast<std::size_t>(idx)];
    if (controlPointIndex == 0 || controlPointIndex + 1 >= original.controlPoints.size())
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "edge split requires an interior control point", "controlPointIndex"));
    if (!std::isfinite(junctionRadius) || junctionRadius <= 0.f)
        return Result<RoadEdgeSplitResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "edge split radius must be positive", "junctionRadius"));

    auto          candidate      = *this;
    const auto    splitPoint     = original.controlPoints[controlPointIndex];
    std::uint32_t insertedNodeId = nodeId;
    if (insertedNodeId == 0) {
        auto node = candidate.addNode(splitPoint.x, splitPoint.y, splitPoint.z, junctionRadius);
        if (!node.ok()) return Result<RoadEdgeSplitResult>::failure(node.status());
        insertedNodeId = node.value();
    } else {
        auto node = candidate.restoreNode({insertedNodeId, splitPoint.x, splitPoint.y, splitPoint.z, junctionRadius});
        if (!node.ok()) return Result<RoadEdgeSplitResult>::failure(node.status());
    }

    auto styles = detail::splitRoadAuthoring(original, controlPointIndex);
    if (!styles.ok()) return Result<RoadEdgeSplitResult>::failure(styles.status());
    auto& first = candidate.edges_[static_cast<std::size_t>(idx)];
    first.style = styles.value().first;
    first.to    = insertedNodeId;
    first.controlPoints.resize(controlPointIndex + 1u);
    first.controlPoints.back() = splitPoint;
    std::vector<RoadControlPoint> continuation(
        original.controlPoints.begin() + static_cast<std::ptrdiff_t>(controlPointIndex), original.controlPoints.end());
    std::uint32_t continuationEdgeId = secondEdgeId;
    if (continuationEdgeId == 0) {
        continuationEdgeId = candidate.nextEdgeId_;
        auto second = candidate.restoreEdge({continuationEdgeId, insertedNodeId, original.to, std::move(continuation),
                                             original.lanesForward, original.lanesBackward, styles.value().second});
        if (!second.ok()) return Result<RoadEdgeSplitResult>::failure(second.status());
    } else {
        auto second = candidate.restoreEdge({continuationEdgeId, insertedNodeId, original.to, std::move(continuation),
                                             original.lanesForward, original.lanesBackward, styles.value().second});
        if (!second.ok()) return Result<RoadEdgeSplitResult>::failure(second.status());
    }

    for (auto& node : candidate.nodes_)
        if (node.id == original.to) {
            for (auto& port : node.connection.ports)
                if (port.edgeId == edgeId) port.edgeId = continuationEdgeId;
            for (auto& route : node.connection.facilityRoutes) {
                if (route.firstEdge == edgeId) route.firstEdge = continuationEdgeId;
                if (route.secondEdge == edgeId) route.secondEdge = continuationEdgeId;
            }
        }

    // The original id remains attached to the `from` half. References whose
    // port lived at the old `to` endpoint migrate to the continuation edge.
    for (auto& link : candidate.laneLinks_) {
        if (link.inEdge == edgeId && link.inDirection == RoadLaneDirection::Forward) link.inEdge = continuationEdgeId;
        if (link.outEdge == edgeId && link.outDirection == RoadLaneDirection::Backward)
            link.outEdge = continuationEdgeId;
    }
    for (auto& link : candidate.blockedLaneLinks_) {
        if (link.inEdge == edgeId && link.inDirection == RoadLaneDirection::Forward) link.inEdge = continuationEdgeId;
        if (link.outEdge == edgeId && link.outDirection == RoadLaneDirection::Backward)
            link.outEdge = continuationEdgeId;
    }
    candidate.normalizeLaneLinkOrder();
    for (int lane = 0; lane < original.lanesForward; ++lane) {
        auto linked = candidate.addLaneLink(
            {edgeId, lane, continuationEdgeId, lane, RoadLaneDirection::Forward, RoadLaneDirection::Forward});
        if (!linked.ok()) return Result<RoadEdgeSplitResult>::failure(linked.status());
    }
    for (int lane = 0; lane < original.lanesBackward; ++lane) {
        auto linked = candidate.addLaneLink(
            {continuationEdgeId, lane, edgeId, lane, RoadLaneDirection::Backward, RoadLaneDirection::Backward});
        if (!linked.ok()) return Result<RoadEdgeSplitResult>::failure(linked.status());
    }
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<RoadEdgeSplitResult>::failure(valid.status());
    candidate.revision_ = revision_ + 1;
    const RoadEdgeSplitResult result{insertedNodeId, edgeId, continuationEdgeId};
    *this = std::move(candidate);
    return Result<RoadEdgeSplitResult>::success(result);
}

Result<RoadEdgeSplitResult> RoadNetwork::splitEdgeAtPosition(std::uint32_t edgeId, RoadControlPoint position,
                                                             float maxDistance, float junctionRadius) {
    return splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius, 0, 0);
}

Result<RoadEdgeSplitResult> RoadNetwork::splitEdgeAtPosition(std::uint32_t edgeId, RoadControlPoint position,
                                                             float maxDistance, float junctionRadius,
                                                             std::uint32_t nodeId, std::uint32_t secondEdgeId) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0)
        return Result<RoadEdgeSplitResult>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    if (!finite3(position.x, position.y, position.z) || !std::isfinite(maxDistance) || maxDistance < 0.f)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "split position and snap distance must be finite", "position"));

    const auto& edge = edges_[static_cast<std::size_t>(idx)];
    auto        path = makeCenterlineSpline(edge);
    if (!path.ok()) return Result<RoadEdgeSplitResult>::failure(path.status());
    const int intervals           = std::min(4096, std::max(32, path.value().segmentCount() * 32));
    float     bestDistanceSquared = std::numeric_limits<float>::max();
    float     bestParameter       = 0.f;
    auto      previous            = path.value().evaluateResult(0.f);
    if (!previous.ok()) return Result<RoadEdgeSplitResult>::failure(previous.status());
    for (int i = 0; i < intervals; ++i) {
        const float nextParameter = static_cast<float>(i + 1) / static_cast<float>(intervals);
        auto        next          = path.value().evaluateResult(nextParameter);
        if (!next.ok()) return Result<RoadEdgeSplitResult>::failure(next.status());
        const RoadControlPoint a{previous.value().x, previous.value().y, previous.value().z};
        const RoadControlPoint b{next.value().x, next.value().y, next.value().z};
        const float            dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float            lengthSquared = dx * dx + dy * dy + dz * dz;
        if (lengthSquared > 1e-8f) {
            const float local = std::clamp(
                ((position.x - a.x) * dx + (position.y - a.y) * dy + (position.z - a.z) * dz) / lengthSquared, 0.f,
                1.f);
            const RoadControlPoint projected{a.x + dx * local, a.y + dy * local, a.z + dz * local};
            const float px = position.x - projected.x, py = position.y - projected.y, pz = position.z - projected.z;
            const float distanceSquared = px * px + py * py + pz * pz;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared           = distanceSquared;
                const float previousParameter = static_cast<float>(i) / static_cast<float>(intervals);
                bestParameter                 = previousParameter + (nextParameter - previousParameter) * local;
            }
        }
        previous = std::move(next);
    }
    // The chord fraction is not a spline parameter fraction, even on a straight
    // Catmull span. Refine on the actual curve before applying the snap limit.
    float lower = std::max(0.f, bestParameter - 1.f / static_cast<float>(intervals));
    float upper = std::min(1.f, bestParameter + 1.f / static_cast<float>(intervals));
    auto squaredDistance = [&](float parameter) -> Result<double> {
        auto point = path.value().evaluateResult(parameter);
        if (!point.ok()) return Result<double>::failure(point.status());
        const double dx = static_cast<double>(point.value().x) - position.x;
        const double dy = static_cast<double>(point.value().y) - position.y;
        const double dz = static_cast<double>(point.value().z) - position.z;
        return Result<double>::success(dx * dx + dy * dy + dz * dz);
    };
    for (int iteration = 0; iteration < 40; ++iteration) {
        const float left = lower + (upper - lower) / 3.f;
        const float right = upper - (upper - lower) / 3.f;
        if (left == right) break;
        auto leftDistance = squaredDistance(left);
        if (!leftDistance.ok()) return Result<RoadEdgeSplitResult>::failure(leftDistance.status());
        auto rightDistance = squaredDistance(right);
        if (!rightDistance.ok()) return Result<RoadEdgeSplitResult>::failure(rightDistance.status());
        if (leftDistance.value() <= rightDistance.value()) upper = right;
        else lower = left;
    }
    bestParameter = lower + (upper - lower) * 0.5f;
    auto actualDistance = squaredDistance(bestParameter);
    if (!actualDistance.ok()) return Result<RoadEdgeSplitResult>::failure(actualDistance.status());
    if (actualDistance.value() > static_cast<double>(maxDistance) * maxDistance)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "position is outside the road snap distance", "maxDistance"));

    constexpr float endpointTolerance = 1e-4f;
    if (bestParameter <= endpointTolerance || bestParameter >= 1.f - endpointTolerance)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "position resolves to an edge endpoint", "position"));
    return splitEdgeAtSplineParameter(edgeId, bestParameter, junctionRadius, nodeId, secondEdgeId);
}


}  // namespace eve::procgen::road
