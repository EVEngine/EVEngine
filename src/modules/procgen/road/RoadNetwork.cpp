#include "procgen/road/RoadNetwork.h"

#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadConnectionInternal.h"
#include "procgen/spline/SplinePath.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

namespace eve::procgen::road {
namespace {
bool laneOrderLess(const RoadLaneConnection& a, const RoadLaneConnection& b) noexcept {
    return std::tie(a.inEdge, a.inDirection, a.inLane, a.outEdge, a.outDirection, a.outLane) <
           std::tie(b.inEdge, b.inDirection, b.inLane, b.outEdge, b.outDirection, b.outLane);
}

bool finite3(float x, float y, float z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

bool validPoint(const RoadControlPoint& point) {
    return finite3(point.x, point.y, point.z) && std::isfinite(point.widthScale) && point.widthScale >= 0.05f &&
           point.widthScale <= 16.f && std::isfinite(point.rollDegrees) && std::fabs(point.rollDegrees) <= 360.f &&
           std::isfinite(point.rollCenterOffset) && point.rollCenterOffset >= -.5f && point.rollCenterOffset <= .5f &&
           std::isfinite(point.splineStrength) && point.splineStrength >= .01f && point.splineStrength <= 1.f &&
           point.pathControl <= RoadPathControl::Circular;
}

Result<void> validateCircularPoints(const std::vector<RoadControlPoint>& points) {
    if (std::none_of(points.begin(), points.end(),
                     [](const auto& p) { return p.pathControl == RoadPathControl::Circular; }))
        return Result<void>::success();
    eve::procgen::SplinePath path;
    for (const auto& p : points) {
        eve::procgen::SplinePoint q;
        q.x               = p.x;
        q.y               = p.y;
        q.z               = p.z;
        q.spanControl     = static_cast<eve::procgen::SplineSpanControl>(p.pathControl);
        q.circularFromEnd = p.circularFromEnd;
        auto added        = path.addPointResult(q);
        if (!added.ok()) return added;
    }
    auto evaluated = path.evaluateResult(0.f);
    if (!evaluated.ok()) return Result<void>::failure(evaluated.status());
    return Result<void>::success();
}

void anchorPoint(RoadControlPoint& point, float x, float y, float z) {
    point.x = x;
    point.y = y;
    point.z = z;
}

bool hasUsableGeometry(const std::vector<RoadControlPoint>& points) {
    constexpr float minimumSegmentLengthSquared = 1e-8f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float dx = points[i].x - points[i - 1].x;
        const float dy = points[i].y - points[i - 1].y;
        const float dz = points[i].z - points[i - 1].z;
        if (dx * dx + dy * dy + dz * dz > minimumSegmentLengthSquared) return true;
    }
    return false;
}


int laneCount(const RoadEdge& edge, RoadLaneDirection direction) {
    return direction == RoadLaneDirection::Forward ? edge.lanesForward : edge.lanesBackward;
}

std::uint32_t laneEntryNode(const RoadEdge& edge, RoadLaneDirection direction) {
    return direction == RoadLaneDirection::Forward ? edge.to : edge.from;
}

std::uint32_t laneExitNode(const RoadEdge& edge, RoadLaneDirection direction) {
    return direction == RoadLaneDirection::Forward ? edge.from : edge.to;
}

bool sameLaneLink(const RoadLaneConnection& a, const RoadLaneConnection& b) {
    return a.inEdge == b.inEdge && a.inLane == b.inLane && a.outEdge == b.outEdge && a.outLane == b.outLane &&
           a.inDirection == b.inDirection && a.outDirection == b.outDirection;
}


}  // namespace

Result<void> RoadNetwork::validateStyle(const RoadStyle& style) const {
    auto profile = makeRoadProfile(style, 1, 0);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    if (!std::isfinite(style.speedLimitMps) || style.speedLimitMps <= 0.f || style.speedLimitMps > 200.f ||
        style.trafficPriority < 0 || style.trafficPriority > 255)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "road traffic settings are outside supported bounds", "style.traffic"));
    const auto& placement = style.sidePlacement;
    if (placement.pathSampling > RoadSidePathSampling::SurfaceSegments ||
        placement.reference > RoadSideReference::Center || !std::isfinite(placement.lateralOffset) ||
        std::fabs(placement.lateralOffset) > 10000.f || !std::isfinite(placement.verticalOffset) ||
        std::fabs(placement.verticalOffset) > 10000.f || !std::isfinite(placement.yawDegrees) ||
        std::fabs(placement.yawDegrees) > 360.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "rigid facility reference or offsets are outside supported bounds",
            "style.sidePlacement"));
    if (style.sideObjectDistribution > RoadSideDistribution::Average || !std::isfinite(style.sideObjectStartOffset) ||
        style.sideObjectStartOffset < 0.f || !std::isfinite(style.sideObjectEndOffset) ||
        style.sideObjectEndOffset < 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "road side-object offsets must be finite and non-negative",
                                                       "style.sideObjects"));
    return Result<void>::success();
}

int mapLaneByLateralRank(int inLane, int inLaneCount, int outLaneCount) {
    if (inLaneCount <= 1) return (outLaneCount - 1) / 2;
    const float rank = static_cast<float>(inLane) / static_cast<float>(inLaneCount - 1);
    return static_cast<int>(std::lround(rank * static_cast<float>(outLaneCount - 1)));
}

bool validJunctionControl(RoadJunctionControl control) {
    return control >= RoadJunctionControl::Uncontrolled && control <= RoadJunctionControl::Signal;
}

int RoadNetwork::findNodeIndex(std::uint32_t id) const {
    const auto it = nodeIndex_.find(id);
    return it == nodeIndex_.end() ? -1 : it->second;
}

int RoadNetwork::findEdgeIndex(std::uint32_t id) const {
    const auto it = edgeIndex_.find(id);
    return it == edgeIndex_.end() ? -1 : it->second;
}

Result<std::uint32_t> RoadNetwork::addNode(float x, float y, float z, float junctionRadius) {
    if (!finite3(x, y, z) || !std::isfinite(junctionRadius) || junctionRadius <= 0.f)
        return Result<std::uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "node requires finite position and positive junctionRadius", "node"));
    const std::uint32_t id = nextNodeId_++;
    nodeIndex_[id]         = static_cast<int>(nodes_.size());
    nodes_.push_back(RoadNode{id, x, y, z, junctionRadius});
    ++revision_;
    return Result<std::uint32_t>::success(id);
}

Result<void> RoadNetwork::restoreNode(RoadNode node) {
    if (node.id == 0 || node.id == std::numeric_limits<std::uint32_t>::max() || findNodeIndex(node.id) >= 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "node id must be non-zero and unused", "node.id"));
    if (!finite3(node.x, node.y, node.z) || !std::isfinite(node.junctionRadius) || node.junctionRadius <= 0.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "node requires finite position and positive junctionRadius", "node"));
    if (!validJunctionControl(node.junctionControl))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "node junction control is outside supported bounds",
                                                       "node.junctionControl"));
    auto asset = detail::validateConnectionAsset(node.connection);
    if (!asset.ok()) return asset;
    const auto position = std::lower_bound(nodes_.begin(), nodes_.end(), node.id,
                                           [](const RoadNode& item, std::uint32_t id) { return item.id < id; });
    const auto inserted = static_cast<std::size_t>(position - nodes_.begin());
    nodes_.insert(position, node);
    for (std::size_t i = inserted; i < nodes_.size(); ++i) nodeIndex_[nodes_[i].id] = static_cast<int>(i);
    nextNodeId_ = std::max(nextNodeId_, node.id + 1);
    ++revision_;
    return Result<void>::success();
}

Result<std::uint32_t> RoadNetwork::addEdge(std::uint32_t from, std::uint32_t to,
                                           std::vector<RoadControlPoint> controlPoints, int lanesForward,
                                           int lanesBackward, RoadStyle style) {
    if (findNodeIndex(from) < 0 || findNodeIndex(to) < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "edge endpoints must reference live nodes", "edge"));
    if (!nodes_[static_cast<std::size_t>(findNodeIndex(from))].connection.ports.empty() ||
        !nodes_[static_cast<std::size_t>(findNodeIndex(to))].connection.ports.empty())
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "clear a custom connection before adding a new incident edge", "edge"));
    if (from == to)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "edge endpoints must be distinct", "edge"));
    if (controlPoints.size() < 2)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "edge needs at least two control points", "edge"));
    for (const auto& p : controlPoints) {
        if (!validPoint(p))
            return Result<std::uint32_t>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "control point positions and profiles must be valid", "edge"));
    }
    auto styleOk = validateStyle(style);
    if (!styleOk.ok()) return Result<std::uint32_t>::failure(styleOk.status());
    auto profile = makeRoadProfile(style, lanesForward, lanesBackward);
    if (!profile.ok()) return Result<std::uint32_t>::failure(profile.status());

    // A node is the authoritative connection position. Canonicalizing the two
    // endpoint samples prevents authored handles from leaving invisible gaps or
    // sending turn curves toward a stale position.
    const auto& fromNode = nodes_[static_cast<std::size_t>(findNodeIndex(from))];
    const auto& toNode   = nodes_[static_cast<std::size_t>(findNodeIndex(to))];
    anchorPoint(controlPoints.front(), fromNode.x, fromNode.y, fromNode.z);
    anchorPoint(controlPoints.back(), toNode.x, toNode.y, toNode.z);
    auto circular = validateCircularPoints(controlPoints);
    if (!circular.ok()) return Result<std::uint32_t>::failure(circular.status());
    if (!hasUsableGeometry(controlPoints))
        return Result<std::uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge centerline must contain a non-degenerate segment", "edge"));

    const std::uint32_t id = nextEdgeId_++;
    edgeIndex_[id]         = static_cast<int>(edges_.size());
    RoadEdge edge;
    edge.id            = id;
    edge.from          = from;
    edge.to            = to;
    edge.controlPoints = std::move(controlPoints);
    edge.lanesForward  = lanesForward;
    edge.lanesBackward = lanesBackward;
    edge.style         = style;
    edges_.push_back(std::move(edge));
    ++revision_;
    return Result<std::uint32_t>::success(id);
}

Result<void> RoadNetwork::restoreEdge(RoadEdge edge) {
    if (edge.id == 0 || edge.id == std::numeric_limits<std::uint32_t>::max() || findEdgeIndex(edge.id) >= 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "edge id must be non-zero and unused", "edge.id"));
    if (findNodeIndex(edge.from) < 0 || findNodeIndex(edge.to) < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "edge endpoints must reference live nodes", "edge"));
    if (edge.from == edge.to)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "edge endpoints must be distinct", "edge"));
    if (edge.controlPoints.size() < 2)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "edge needs at least two control points", "edge"));
    for (const auto& point : edge.controlPoints) {
        if (!validPoint(point))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "control point positions and profiles must be valid", "edge"));
    }
    auto styleOk = validateStyle(edge.style);
    if (!styleOk.ok()) return styleOk;
    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    const auto& from = nodes_[static_cast<std::size_t>(findNodeIndex(edge.from))];
    const auto& to   = nodes_[static_cast<std::size_t>(findNodeIndex(edge.to))];
    anchorPoint(edge.controlPoints.front(), from.x, from.y, from.z);
    anchorPoint(edge.controlPoints.back(), to.x, to.y, to.z);
    auto circular = validateCircularPoints(edge.controlPoints);
    if (!circular.ok()) return circular;
    if (!hasUsableGeometry(edge.controlPoints))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge centerline must contain a non-degenerate segment", "edge"));
    nextEdgeId_         = std::max(nextEdgeId_, edge.id + 1);
    const auto position = std::lower_bound(edges_.begin(), edges_.end(), edge.id,
                                           [](const RoadEdge& item, std::uint32_t id) { return item.id < id; });
    const auto inserted = static_cast<std::size_t>(position - edges_.begin());
    edges_.insert(position, std::move(edge));
    for (std::size_t i = inserted; i < edges_.size(); ++i) edgeIndex_[edges_[i].id] = static_cast<int>(i);
    ++revision_;
    return Result<void>::success();
}

Result<void> RoadNetwork::validateLaneConnection(const RoadLaneConnection& link) const {
    const int inIdx  = findEdgeIndex(link.inEdge);
    const int outIdx = findEdgeIndex(link.outEdge);
    if (inIdx < 0 || outIdx < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lane link references missing edges", "laneLink"));
    const auto& inEdge  = edges_[static_cast<std::size_t>(inIdx)];
    const auto& outEdge = edges_[static_cast<std::size_t>(outIdx)];
    const auto  sharedNode = laneEntryNode(inEdge, link.inDirection);
    if (sharedNode != laneExitNode(outEdge, link.outDirection))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "lane link directions must meet at the same node", "laneLink"));
    if (link.inEdge == link.outEdge)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "lane link cannot make an immediate U-turn on one edge",
                                                       "laneLink"));
    if (link.inLane < 0 || link.inLane >= laneCount(inEdge, link.inDirection))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "inLane out of range", "laneLink"));
    if (link.outLane < 0 || link.outLane >= laneCount(outEdge, link.outDirection))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "outLane out of range", "laneLink"));
    return Result<void>::success();
}

Result<void> RoadNetwork::addLaneLink(RoadLaneConnection link) {
    auto valid = validateLaneConnection(link);
    if (!valid.ok()) return valid;
    if (std::any_of(laneLinks_.begin(), laneLinks_.end(),
                    [&](const auto& existing) { return sameLaneLink(existing, link); }))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "lane link already exists", "laneLink"));
    if (std::any_of(blockedLaneLinks_.begin(), blockedLaneLinks_.end(),
                    [&](const auto& blocked) { return sameLaneLink(blocked, link); }))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "lane link is persistently blocked", "laneLink"));
    laneLinks_.insert(std::lower_bound(laneLinks_.begin(), laneLinks_.end(), link, laneOrderLess), link);
    ++revision_;
    return Result<void>::success();
}

Result<bool> RoadNetwork::blockLaneLink(RoadLaneConnection link) {
    auto valid = validateLaneConnection(link);
    if (!valid.ok()) return Result<bool>::failure(valid.status());
    if (std::any_of(blockedLaneLinks_.begin(), blockedLaneLinks_.end(),
                    [&](const auto& blocked) { return sameLaneLink(blocked, link); }))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "lane link is already blocked", "laneLink"));
    auto candidate = *this;
    const auto active    = std::find_if(candidate.laneLinks_.begin(), candidate.laneLinks_.end(),
                                        [&](const auto& current) { return sameLaneLink(current, link); });
    const bool removed   = active != candidate.laneLinks_.end();
    if (removed) candidate.laneLinks_.erase(active);
    candidate.blockedLaneLinks_.insert(
        std::lower_bound(candidate.blockedLaneLinks_.begin(), candidate.blockedLaneLinks_.end(), link, laneOrderLess),
        link);
    auto checked = candidate.validate();
    if (!checked.ok()) return Result<bool>::failure(checked.status());
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return Result<bool>::success(removed);
}

Result<void> RoadNetwork::unblockLaneLink(const RoadLaneConnection& link) {
    const auto found = std::find_if(blockedLaneLinks_.begin(), blockedLaneLinks_.end(),
                                    [&](const auto& blocked) { return sameLaneLink(blocked, link); });
    if (found == blockedLaneLinks_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lane link block does not exist", "laneLink"));
    blockedLaneLinks_.erase(found);
    ++revision_;
    return Result<void>::success();
}

Result<void> RoadNetwork::removeLaneLink(const RoadLaneConnection& link) {
    const auto found = std::find_if(laneLinks_.begin(), laneLinks_.end(),
                                    [&](const RoadLaneConnection& candidate) { return sameLaneLink(candidate, link); });
    if (found == laneLinks_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lane link does not exist", "laneLink"));
    laneLinks_.erase(found);
    ++revision_;
    return Result<void>::success();
}

Result<int> RoadNetwork::connectAllTurns(std::uint32_t nodeId) {
    if (findNodeIndex(nodeId) < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    auto candidate = *this;
    int added = 0;
    constexpr RoadLaneDirection directions[] = {RoadLaneDirection::Forward, RoadLaneDirection::Backward};
    for (const auto& inEdge : candidate.edges_) {
        for (const auto inDirection : directions) {
            const int inLanes = laneCount(inEdge, inDirection);
            if (inLanes == 0 || laneEntryNode(inEdge, inDirection) != nodeId) continue;
            for (const auto& outEdge : candidate.edges_) {
                if (outEdge.id == inEdge.id) continue;
                for (const auto outDirection : directions) {
                    const int outLanes = laneCount(outEdge, outDirection);
                    if (outLanes == 0 || laneExitNode(outEdge, outDirection) != nodeId) continue;
                    for (int il = 0; il < inLanes; ++il) {
                        // Preserve normalized lateral rank when lane counts change. This
                        // distributes merges/expansions across the full destination width
                        // instead of collapsing every overflow lane onto one side.
                        const int          ol = mapLaneByLateralRank(il, inLanes, outLanes);
                        RoadLaneConnection link{inEdge.id, il, outEdge.id, ol, inDirection, outDirection};
                        const bool blocked = std::any_of(candidate.blockedLaneLinks_.begin(),
                                                         candidate.blockedLaneLinks_.end(),
                                                         [&](const auto& current) {
                                                             return sameLaneLink(current, link);
                                                         });
                        if (blocked) continue;
                        // Any authored link for the same incoming lane and outgoing
                        // physical route is an override, even when it selects another
                        // destination lane. Do not re-add an automatic parallel turn.
                        const bool overridden =
                            std::any_of(candidate.laneLinks_.begin(), candidate.laneLinks_.end(), [&](const auto& current) {
                                return current.inEdge == link.inEdge && current.inLane == link.inLane &&
                                       current.outEdge == link.outEdge && current.inDirection == link.inDirection &&
                                       current.outDirection == link.outDirection;
                            });
                        if (overridden) continue;
                        auto r = candidate.addLaneLink(link);
                        if (!r.ok()) return Result<int>::failure(r.status());
                        ++added;
                    }
                }
            }
        }
    }
    if (added == 0) return Result<int>::success(0);
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<int>::failure(valid.status());
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return Result<int>::success(added);
}

Result<void> RoadNetwork::validate() const {
    for (const auto& node : nodes_) {
        if (!finite3(node.x, node.y, node.z) || !std::isfinite(node.junctionRadius) || node.junctionRadius <= 0.f ||
            !validJunctionControl(node.junctionControl))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "network contains an invalid junction node", "node"));
    }
    for (const auto& node : nodes_) {
        auto connection = detail::validateConnectionSeams(*this, node);
        if (!connection.ok()) return connection;
        auto routes = detail::validateFacilityRoutes(*this, node);
        if (!routes.ok()) return routes;
    }
    for (const auto& edge : edges_) {
        for (const auto& point : edge.controlPoints) {
            if (!validPoint(point))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                               "edge contains invalid control-point profiles", "edge"));
        }
        auto circular = validateCircularPoints(edge.controlPoints);
        if (!circular.ok()) return circular;
        auto styleOk = validateStyle(edge.style);
        if (!styleOk.ok()) return styleOk;
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) return Result<void>::failure(profile.status());
        if (findNodeIndex(edge.from) < 0 || findNodeIndex(edge.to) < 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "edge references a missing node", "edge"));
        if (edge.from == edge.to)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "edge forms a self-loop", "edge"));
        if (edge.controlPoints.size() < 2)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "edge has fewer than two samples", "edge"));
        if (!hasUsableGeometry(edge.controlPoints))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "edge centerline has no usable segment", "edge"));
        const auto& a  = nodes_[static_cast<std::size_t>(findNodeIndex(edge.from))];
        const auto& b  = nodes_[static_cast<std::size_t>(findNodeIndex(edge.to))];
        const auto& p0 = edge.controlPoints.front();
        const auto& p1 = edge.controlPoints.back();
        if (p0.x != a.x || p0.y != a.y || p0.z != a.z || p1.x != b.x || p1.y != b.y || p1.z != b.z)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "edge samples are not anchored to endpoint nodes", "edge"));
    }
    for (std::size_t i = 0; i < laneLinks_.size(); ++i) {
        const auto& link   = laneLinks_[i];
        const int   inIdx  = findEdgeIndex(link.inEdge);
        const int   outIdx = findEdgeIndex(link.outEdge);
        if (inIdx < 0 || outIdx < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "lane link references a missing edge", "laneLink"));
        const auto& inEdge  = edges_[static_cast<std::size_t>(inIdx)];
        const auto& outEdge = edges_[static_cast<std::size_t>(outIdx)];
        if (laneEntryNode(inEdge, link.inDirection) != laneExitNode(outEdge, link.outDirection) || link.inLane < 0 ||
            link.inLane >= laneCount(inEdge, link.inDirection) || link.outLane < 0 ||
            link.outLane >= laneCount(outEdge, link.outDirection))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "lane link violates endpoint or lane bounds", "laneLink"));
        for (std::size_t j = i + 1; j < laneLinks_.size(); ++j) {
            if (sameLaneLink(link, laneLinks_[j]))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                               "network contains a duplicate lane link", "laneLink"));
        }
    }
    return Result<void>::success();
}

Result<void> RoadNetwork::setEdgeStyle(std::uint32_t edgeId, RoadStyle style) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    auto styleOk = validateStyle(style);
    if (!styleOk.ok()) return styleOk;
    auto profile = makeRoadProfile(style, edges_[static_cast<std::size_t>(idx)].lanesForward,
                                   edges_[static_cast<std::size_t>(idx)].lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    const auto& original = edges_[static_cast<std::size_t>(idx)];
    if (!nodes_[static_cast<std::size_t>(findNodeIndex(original.from))].connection.ports.empty() ||
        !nodes_[static_cast<std::size_t>(findNodeIndex(original.to))].connection.ports.empty() ||
        !nodes_[static_cast<std::size_t>(findNodeIndex(original.from))].connection.facilityRoutes.empty() ||
        !nodes_[static_cast<std::size_t>(findNodeIndex(original.to))].connection.facilityRoutes.empty()) {
        auto candidate                                        = *this;
        candidate.edges_[static_cast<std::size_t>(idx)].style = std::move(style);
        auto valid                                            = candidate.validate();
        if (!valid.ok()) return valid;
        candidate.revision_ = revision_ + 1;
        *this               = std::move(candidate);
        return Result<void>::success();
    }
    edges_[static_cast<std::size_t>(idx)].style = style;
    ++revision_;
    return Result<void>::success();
}

Result<void> RoadNetwork::setNodePosition(std::uint32_t nodeId, float x, float y, float z) {
    const int index = findNodeIndex(nodeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    if (!finite3(x, y, z))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "node position must be finite", "position"));

    const auto& node = nodes_[static_cast<std::size_t>(index)];
    if (node.x == x && node.y == y && node.z == z) return Result<void>::success();
    auto candidate = *this;
    auto& candidateNode = candidate.nodes_[static_cast<std::size_t>(index)];
    candidateNode.x = x;
    candidateNode.y     = y;
    candidateNode.z     = z;
    for (auto& edge : candidate.edges_) {
        if (edge.from == nodeId) anchorPoint(edge.controlPoints.front(), x, y, z);
        if (edge.to == nodeId) anchorPoint(edge.controlPoints.back(), x, y, z);
    }
    for (std::size_t i = 0; i < blockedLaneLinks_.size(); ++i) {
        const auto& blocked = blockedLaneLinks_[i];
        auto valid = validateLaneConnection(blocked);
        if (!valid.ok())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "blocked lane link violates topology", "blockedLaneLink"));
        if (std::any_of(laneLinks_.begin(), laneLinks_.end(),
                        [&](const auto& active) { return sameLaneLink(active, blocked); }))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "lane link is both active and blocked", "blockedLaneLink"));
        for (std::size_t j = i + 1; j < blockedLaneLinks_.size(); ++j) {
            if (sameLaneLink(blocked, blockedLaneLinks_[j]))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvariantViolation, "network contains a duplicate lane-link block",
                    "blockedLaneLink"));
        }
    }
    auto valid = candidate.validate();
    if (!valid.ok()) return valid;
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return Result<void>::success();
}

Result<void> RoadNetwork::setNodeJunctionRadius(std::uint32_t nodeId, float junctionRadius) {
    const int index = findNodeIndex(nodeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    if (!std::isfinite(junctionRadius) || junctionRadius <= 0.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "junction radius must be finite and positive", "junctionRadius"));
    auto& node = nodes_[static_cast<std::size_t>(index)];
    if (node.junctionRadius == junctionRadius) return Result<void>::success();
    node.junctionRadius = junctionRadius;
    ++revision_;
    return Result<void>::success();
}

Result<void> RoadNetwork::setNodeJunctionControl(std::uint32_t nodeId, RoadJunctionControl control) {
    const int index = findNodeIndex(nodeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    if (!validJunctionControl(control))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "junction control is outside supported bounds", "junctionControl"));
    auto& node = nodes_[static_cast<std::size_t>(index)];
    if (node.junctionControl == control) return Result<void>::success();
    node.junctionControl = control;
    ++revision_;
    return Result<void>::success();
}

Result<int> RoadNetwork::mergeNodes(std::uint32_t keepNodeId, std::uint32_t removeNodeId, float maxDistance) {
    if (keepNodeId == removeNodeId)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "merge nodes must be distinct", "removeNodeId"));
    const int keepIndex = findNodeIndex(keepNodeId), removeIndex = findNodeIndex(removeNodeId);
    if (keepIndex < 0 || removeIndex < 0)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "merge references an unknown node", "nodeId"));
    if (!std::isfinite(maxDistance) || maxDistance < 0.f)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "merge distance must be finite and non-negative",
                              "maxDistance"));
    const auto& keep = nodes_[static_cast<std::size_t>(keepIndex)];
    const auto& removed = nodes_[static_cast<std::size_t>(removeIndex)];
    const float dx = keep.x - removed.x, dy = keep.y - removed.y, dz = keep.z - removed.z;
    if (dx * dx + dy * dy + dz * dz > maxDistance * maxDistance)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "nodes are outside the merge distance", "maxDistance"));
    if (std::any_of(edges_.begin(), edges_.end(), [&](const auto& edge) {
            return (edge.from == keepNodeId && edge.to == removeNodeId) ||
                   (edge.from == removeNodeId && edge.to == keepNodeId);
        }))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                      "merging nodes would create a self-loop edge", "removeNodeId"));

    if (!nodes_[static_cast<std::size_t>(keepIndex)].connection.ports.empty() ||
        !nodes_[static_cast<std::size_t>(removeIndex)].connection.ports.empty() ||
        !nodes_[static_cast<std::size_t>(keepIndex)].connection.facilityRoutes.empty() ||
        !nodes_[static_cast<std::size_t>(removeIndex)].connection.facilityRoutes.empty())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                      "clear custom connection assets before merging their nodes",
                                                      "nodeId"));
    auto candidate = *this;
    int  rewired   = 0;
    for (auto& edge : candidate.edges_) {
        if (edge.from == removeNodeId) {
            edge.from = keepNodeId;
            anchorPoint(edge.controlPoints.front(), keep.x, keep.y, keep.z);
            ++rewired;
        }
        if (edge.to == removeNodeId) {
            edge.to = keepNodeId;
            anchorPoint(edge.controlPoints.back(), keep.x, keep.y, keep.z);
            ++rewired;
        }
    }
    candidate.nodes_.erase(candidate.nodes_.begin() + removeIndex);
    candidate.nodeIndex_.clear();
    for (std::size_t i = 0; i < candidate.nodes_.size(); ++i)
        candidate.nodeIndex_[candidate.nodes_[i].id] = static_cast<int>(i);
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<int>::failure(valid.status());
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return Result<int>::success(rewired);
}

Result<void> RoadNetwork::setEdgeControlPoints(std::uint32_t edgeId, std::vector<RoadControlPoint> controlPoints) {
    const int index = findEdgeIndex(edgeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    if (controlPoints.size() < 2)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "edge needs at least two control points", "points"));
    for (const auto& point : controlPoints) {
        if (!validPoint(point))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "control point positions and profiles must be valid", "points"));
    }

    auto&       edge = edges_[static_cast<std::size_t>(index)];
    const auto& from = nodes_[static_cast<std::size_t>(findNodeIndex(edge.from))];
    const auto& to   = nodes_[static_cast<std::size_t>(findNodeIndex(edge.to))];
    anchorPoint(controlPoints.front(), from.x, from.y, from.z);
    anchorPoint(controlPoints.back(), to.x, to.y, to.z);
    auto circular = validateCircularPoints(controlPoints);
    if (!circular.ok()) return circular;
    if (!hasUsableGeometry(controlPoints))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge centerline must contain a non-degenerate segment", "points"));
    if (!from.connection.ports.empty() || !to.connection.ports.empty()) {
        auto candidate                                                  = *this;
        candidate.edges_[static_cast<std::size_t>(index)].controlPoints = std::move(controlPoints);
        auto valid                                                      = candidate.validate();
        if (!valid.ok()) return valid;
        candidate.revision_ = revision_ + 1;
        *this               = std::move(candidate);
        return Result<void>::success();
    }
    edge.controlPoints = std::move(controlPoints);
    ++revision_;
    return Result<void>::success();
}

Result<int> RoadNetwork::setEdgeLaneCounts(std::uint32_t edgeId, int lanesForward, int lanesBackward) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0) return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    auto profile = makeRoadProfile(edges_[static_cast<std::size_t>(idx)].style, lanesForward, lanesBackward);
    if (!profile.ok()) return Result<int>::failure(profile.status());

    auto candidate = *this;
    auto& edge     = candidate.edges_[static_cast<std::size_t>(idx)];
    edge.lanesForward  = lanesForward;
    edge.lanesBackward = lanesBackward;
    const auto invalid = [&](const RoadLaneConnection& link) {
        if (link.inEdge == edgeId && link.inLane >= laneCount(edge, link.inDirection)) return true;
        return link.outEdge == edgeId && link.outLane >= laneCount(edge, link.outDirection);
    };
    const auto before = candidate.laneLinks_.size();
    candidate.laneLinks_.erase(std::remove_if(candidate.laneLinks_.begin(), candidate.laneLinks_.end(), invalid),
                               candidate.laneLinks_.end());
    candidate.blockedLaneLinks_.erase(
        std::remove_if(candidate.blockedLaneLinks_.begin(), candidate.blockedLaneLinks_.end(), invalid),
        candidate.blockedLaneLinks_.end());
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<int>::failure(valid.status());
    candidate.revision_ = revision_ + 1;
    const int removed = static_cast<int>(before - candidate.laneLinks_.size());
    *this             = std::move(candidate);
    return Result<int>::success(removed);
}

Result<void> RoadNetwork::reverseEdge(std::uint32_t edgeId) {
    const int index = findEdgeIndex(edgeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    auto candidate = *this;
    auto& edge      = candidate.edges_[static_cast<std::size_t>(index)];
    std::swap(edge.from, edge.to);
    std::reverse(edge.controlPoints.begin(), edge.controlPoints.end());
    // Strength belongs to outgoing spans, not the geometric marker after reversal.
    const float terminalStrength = edge.controlPoints.front().splineStrength;
    const auto  terminalControl  = edge.controlPoints.front().pathControl;
    const bool  terminalStart    = edge.controlPoints.front().alignStraightStart,
               terminalEnd       = edge.controlPoints.front().alignStraightEnd;
    const bool terminalCircle    = edge.controlPoints.front().circularFromEnd;
    for (std::size_t i = 0; i + 1 < edge.controlPoints.size(); ++i) {
        edge.controlPoints[i].splineStrength     = edge.controlPoints[i + 1].splineStrength;
        edge.controlPoints[i].pathControl        = edge.controlPoints[i + 1].pathControl;
        edge.controlPoints[i].circularFromEnd    = edge.controlPoints[i].pathControl == RoadPathControl::Circular
                                                       ? !edge.controlPoints[i + 1].circularFromEnd
                                                       : edge.controlPoints[i + 1].circularFromEnd;
        edge.controlPoints[i].alignStraightStart = edge.controlPoints[i + 1].alignStraightEnd;
        edge.controlPoints[i].alignStraightEnd   = edge.controlPoints[i + 1].alignStraightStart;
    }
    edge.controlPoints.back().splineStrength = terminalStrength;
    edge.controlPoints.back().pathControl    = terminalControl;
    edge.controlPoints.back().circularFromEnd =
        terminalControl == RoadPathControl::Circular ? !terminalCircle : terminalCircle;
    edge.controlPoints.back().alignStraightStart = terminalEnd;
    edge.controlPoints.back().alignStraightEnd   = terminalStart;
    for (auto& point : edge.controlPoints) {
        if (point.rollDegrees != 0.f) point.rollDegrees = -point.rollDegrees;
        if (point.rollCenterOffset != 0.f) point.rollCenterOffset = -point.rollCenterOffset;
    }
    detail::reverseRoadAuthoring(edge.style);
    std::swap(edge.lanesForward, edge.lanesBackward);
    std::swap(edge.style.sideObjectStartOffset, edge.style.sideObjectEndOffset);
    std::swap(edge.style.sideObjectsLeft, edge.style.sideObjectsRight);
    const auto flip = [](RoadLaneDirection direction) {
        return direction == RoadLaneDirection::Forward ? RoadLaneDirection::Backward : RoadLaneDirection::Forward;
    };
    for (auto& link : candidate.laneLinks_) {
        if (link.inEdge == edgeId) link.inDirection = flip(link.inDirection);
        if (link.outEdge == edgeId) link.outDirection = flip(link.outDirection);
    }
    for (auto& link : candidate.blockedLaneLinks_) {
        if (link.inEdge == edgeId) link.inDirection = flip(link.inDirection);
        if (link.outEdge == edgeId) link.outDirection = flip(link.outDirection);
    }
    candidate.normalizeLaneLinkOrder();
    auto valid = candidate.validate();
    if (!valid.ok()) return valid;
    candidate.revision_ = revision_ + 1;
    *this               = std::move(candidate);
    return Result<void>::success();
}
void RoadNetwork::normalizeLaneLinkOrder() {
    std::sort(laneLinks_.begin(), laneLinks_.end(), laneOrderLess);
    std::sort(blockedLaneLinks_.begin(), blockedLaneLinks_.end(), laneOrderLess);
}

Result<int> RoadNetwork::reconnectEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) {
    const int edgeIndex = findEdgeIndex(edgeId);
    const int nodeIndex = findNodeIndex(nodeId);
    if (edgeIndex < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    if (nodeIndex < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    const auto& current = edges_[static_cast<std::size_t>(edgeIndex)];
    const std::uint32_t oldNodeId = fromEndpoint ? current.from : current.to;
    if (oldNodeId == nodeId) return Result<int>::success(0);
    if ((fromEndpoint ? current.to : current.from) == nodeId)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge endpoint reconnect would create a self-loop", "nodeId"));

    auto candidate = *this;
    auto& edge = candidate.edges_[static_cast<std::size_t>(edgeIndex)];
    const auto& node      = candidate.nodes_[static_cast<std::size_t>(nodeIndex)];
    if (fromEndpoint) {
        edge.from = nodeId;
        anchorPoint(edge.controlPoints.front(), node.x, node.y, node.z);
    } else {
        edge.to = nodeId;
        anchorPoint(edge.controlPoints.back(), node.x, node.y, node.z);
    }
    const auto stale = [&](const RoadLaneConnection& link) {
        if (link.inEdge != edgeId && link.outEdge != edgeId) return false;
        return !candidate.validateLaneConnection(link).ok();
    };
    const auto activeBefore = candidate.laneLinks_.size();
    const auto blockedBefore = candidate.blockedLaneLinks_.size();
    candidate.laneLinks_.erase(std::remove_if(candidate.laneLinks_.begin(), candidate.laneLinks_.end(), stale),
                               candidate.laneLinks_.end());
    candidate.blockedLaneLinks_.erase(
        std::remove_if(candidate.blockedLaneLinks_.begin(), candidate.blockedLaneLinks_.end(), stale),
        candidate.blockedLaneLinks_.end());
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<int>::failure(valid.status());
    candidate.revision_ = revision_ + 1;
    const int removed = static_cast<int>((activeBefore - candidate.laneLinks_.size()) +
                                         (blockedBefore - candidate.blockedLaneLinks_.size()));
    *this = std::move(candidate);
    return Result<int>::success(removed);
}

Result<std::uint32_t> RoadNetwork::detachEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint) {
    return detachEdgeEndpoint(edgeId, fromEndpoint, 0);
}

Result<std::uint32_t> RoadNetwork::detachEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint,
                                                      std::uint32_t nodeId) {
    const int edgeIndex = findEdgeIndex(edgeId);
    if (edgeIndex < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    const auto& edge = edges_[static_cast<std::size_t>(edgeIndex)];
    const std::uint32_t oldNodeId = fromEndpoint ? edge.from : edge.to;
    const auto incidentCount = std::count_if(edges_.begin(), edges_.end(), [&](const RoadEdge& candidate) {
        return candidate.from == oldNodeId || candidate.to == oldNodeId;
    });
    if (incidentCount < 2)
        return Result<std::uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation,
            "edge endpoint must share a junction with another edge before detaching", "edgeId"));
    const int oldNodeIndex = findNodeIndex(oldNodeId);
    if (oldNodeIndex < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "edge endpoint node is missing", "edgeId"));
    const auto oldNode = nodes_[static_cast<std::size_t>(oldNodeIndex)];

    auto candidate = *this;
    std::uint32_t createdNodeId = nodeId;
    if (createdNodeId == 0) {
        auto created = candidate.addNode(oldNode.x, oldNode.y, oldNode.z, oldNode.junctionRadius);
        if (!created.ok()) return Result<std::uint32_t>::failure(created.status());
        createdNodeId = created.value();
    } else {
        auto restored = candidate.restoreNode(
            {createdNodeId, oldNode.x, oldNode.y, oldNode.z, oldNode.junctionRadius});
        if (!restored.ok()) return Result<std::uint32_t>::failure(restored.status());
    }
    auto reconnected = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, createdNodeId);
    if (!reconnected.ok()) return Result<std::uint32_t>::failure(reconnected.status());
    candidate.revision_ = revision_ + 1;
    const auto result = createdNodeId;
    *this = std::move(candidate);
    return Result<std::uint32_t>::success(result);
}

Result<void> RoadNetwork::removeEdge(std::uint32_t edgeId) {
    for (const auto& node : nodes_)
        for (const auto& route : node.connection.facilityRoutes)
            if (route.firstEdge == edgeId || route.secondEdge == edgeId)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                               "clear facility routes before removing a bound edge",
                                                               "edgeId"));
    for (const auto& node : nodes_)
        for (const auto& port : node.connection.ports)
            if (port.edgeId == edgeId)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                      "clear the custom connection before destroying its bound edge", "edgeId"));
    const int index = findEdgeIndex(edgeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    laneLinks_.erase(
        std::remove_if(laneLinks_.begin(), laneLinks_.end(),
                       [edgeId](const auto& link) { return link.inEdge == edgeId || link.outEdge == edgeId; }),
        laneLinks_.end());
    blockedLaneLinks_.erase(
        std::remove_if(blockedLaneLinks_.begin(), blockedLaneLinks_.end(),
                       [edgeId](const auto& link) { return link.inEdge == edgeId || link.outEdge == edgeId; }),
        blockedLaneLinks_.end());
    edges_.erase(edges_.begin() + index);
    edgeIndex_.clear();
    for (std::size_t i = 0; i < edges_.size(); ++i) edgeIndex_[edges_[i].id] = static_cast<int>(i);
    ++revision_;
    return Result<void>::success();
}

Result<void> RoadNetwork::removeNode(std::uint32_t nodeId) {
    const int index = findNodeIndex(nodeId);
    if (index < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    if (std::any_of(edges_.begin(), edges_.end(),
                    [nodeId](const auto& edge) { return edge.from == nodeId || edge.to == nodeId; }))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "node must be isolated before removal", "nodeId"));
    nodes_.erase(nodes_.begin() + index);
    nodeIndex_.clear();
    for (std::size_t i = 0; i < nodes_.size(); ++i) nodeIndex_[nodes_[i].id] = static_cast<int>(i);
    ++revision_;
    return Result<void>::success();
}

void RoadNetwork::clear() {
    nodes_.clear();
    edges_.clear();
    laneLinks_.clear();
    blockedLaneLinks_.clear();
    nodeIndex_.clear();
    edgeIndex_.clear();
    nextNodeId_ = 1;
    nextEdgeId_ = 1;
    ++revision_;
}

Result<RoadNode> RoadNetwork::nodeResult(std::uint32_t id) const {
    const int idx = findNodeIndex(id);
    if (idx < 0) return Result<RoadNode>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "id"));
    return Result<RoadNode>::success(nodes_[static_cast<std::size_t>(idx)]);
}

Result<RoadEdge> RoadNetwork::edgeResult(std::uint32_t id) const {
    const int idx = findEdgeIndex(id);
    if (idx < 0) return Result<RoadEdge>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "id"));
    return Result<RoadEdge>::success(edges_[static_cast<std::size_t>(idx)]);
}

}  // namespace eve::procgen::road
