#include "procgen/road/RoadNetwork.h"

#include "procgen/spline/SplinePath.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::procgen::road {
namespace {

bool finite3(float x, float y, float z) { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

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

Result<SplinePath> makeCenterlineSpline(const RoadEdge& edge) {
    SplinePath path;
    auto kind = path.setKindResult("catmullRom");
    if (!kind.ok()) return Result<SplinePath>::failure(kind.status());
    path.setClosed(false);
    for (const auto& point : edge.controlPoints) {
        SplinePoint splinePoint;
        splinePoint.x = point.x;
        splinePoint.y = point.y;
        splinePoint.z = point.z;
        auto added = path.addPointResult(splinePoint);
        if (!added.ok()) return Result<SplinePath>::failure(added.status());
    }
    return Result<SplinePath>::success(std::move(path));
}

RoadControlPoint P(float x, float y, float z) { return RoadControlPoint{x, y, z}; }

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

RoadStyle groundStyle() {
    RoadStyle style;
    style.deckThickness = 0.35f;
    style.pierClearance = 100.f;
    style.curbHeight    = 0.50f;
    style.curbWidth     = 0.42f;
    return style;
}

RoadStyle bridgeStyle() {
    RoadStyle style     = groundStyle();
    style.deckThickness = 0.75f;
    style.pierClearance = 1.25f;
    style.pierSpacing   = 8.f;
    style.pierWidth     = 1.3f;
    style.pierDepth     = 1.3f;
    style.curbHeight    = 0.55f;
    return style;
}

Result<RoadNetwork> makeTightTurnScene(int lanes) {
    if (lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanes in [1,4] required", "tight-turn"));

    RoadNetwork network;
    RoadStyle   style = groundStyle();
    style.laneWidth   = 1.2f;
    style.sidewalkWidth = 0.25f;

    auto incomingStart = network.addNode(-9.f, 0.f, -3.f, 1.f);
    auto hub           = network.addNode(0.f, 0.f, 0.f, 8.f);
    auto outgoingEnd   = network.addNode(-9.f, 0.f, 3.f, 1.f);
    for (auto* node : {&incomingStart, &hub, &outgoingEnd}) {
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    }

    auto incoming = network.addEdge(incomingStart.value(), hub.value(),
                                    {P(-9.f, 0.f, -3.f), P(0.f, 0.f, 0.f)}, lanes, 0, style);
    auto outgoing = network.addEdge(hub.value(), outgoingEnd.value(),
                                    {P(0.f, 0.f, 0.f), P(-9.f, 0.f, 3.f)}, lanes, 0, style);
    if (!incoming.ok()) return Result<RoadNetwork>::failure(incoming.status());
    if (!outgoing.ok()) return Result<RoadNetwork>::failure(outgoing.status());
    auto turn = network.addLaneLink({incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                     RoadLaneDirection::Forward});
    if (!turn.ok()) return Result<RoadNetwork>::failure(turn.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> makeThreeArmScene(const std::string& scene, int lanes) {
    if (lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "lanes in [1,4] required", scene));

    RoadNetwork network;
    RoadStyle   narrow = groundStyle();
    RoadStyle   wide   = groundStyle();
    narrow.laneWidth   = 3.f;
    wide.laneWidth     = 4.2f;

    const bool yJunction = scene == "y-junction";
    auto hub   = network.addNode(0.f, 0.f, 0.f, yJunction ? 5.f : 6.f);
    auto stem  = network.addNode(0.f, 0.f, 24.f, 2.f);
    auto left  = network.addNode(-18.f, 0.f, yJunction ? -18.f : 0.f, 2.f);
    auto right = network.addNode(18.f, 0.f, yJunction ? -18.f : 0.f, 2.f);
    for (auto* node : {&hub, &stem, &left, &right}) {
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    }
    auto incoming = network.addEdge(stem.value(), hub.value(), {P(0.f, 0.f, 24.f), P(0.f, 0.f, 0.f)}, lanes,
                                    yJunction && lanes > 1 ? 1 : 0, yJunction ? wide : narrow);
    auto west = network.addEdge(hub.value(), left.value(),
                                {P(0.f, 0.f, 0.f), P(-18.f, 0.f, yJunction ? -18.f : 0.f)}, lanes, 0, narrow);
    auto east = network.addEdge(hub.value(), right.value(),
                                {P(0.f, 0.f, 0.f), P(18.f, 0.f, yJunction ? -18.f : 0.f)},
                                yJunction ? std::min(4, lanes + 1) : lanes, 0, yJunction ? wide : narrow);
    for (auto* edge : {&incoming, &west, &east}) {
        if (!edge->ok()) return Result<RoadNetwork>::failure(edge->status());
    }
    auto turns = network.connectAllTurns(hub.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> makeSlopedJunctionScene(int lanes, bool curved) {
    if (lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "lanes in [1,4] required", curved ? "curve-uphill" : "sloped-t"));

    RoadNetwork network;
    RoadStyle   style = groundStyle();
    auto hub  = network.addNode(0.f, 4.f, 0.f, 6.f);
    auto low  = network.addNode(-22.f, 0.f, 12.f, 2.f);
    auto high = network.addNode(-20.f, 7.f, -14.f, 2.f);
    auto exit = network.addNode(24.f, 4.f, 0.f, 2.f);
    for (auto* node : {&hub, &low, &high, &exit}) {
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    }
    const std::vector<RoadControlPoint> lowPoints =
        curved ? std::vector<RoadControlPoint>{P(-22.f, 0.f, 12.f), P(-13.f, 1.4f, 14.f), P(-6.f, 3.f, 7.f),
                                               P(0.f, 4.f, 0.f)}
               : std::vector<RoadControlPoint>{P(-22.f, 0.f, 12.f), P(0.f, 4.f, 0.f)};
    const std::vector<RoadControlPoint> highPoints =
        curved ? std::vector<RoadControlPoint>{P(-20.f, 7.f, -14.f), P(-12.f, 6.4f, -16.f), P(-5.f, 5.f, -7.f),
                                               P(0.f, 4.f, 0.f)}
               : std::vector<RoadControlPoint>{P(-20.f, 7.f, -14.f), P(0.f, 4.f, 0.f)};
    auto lowEdge  = network.addEdge(low.value(), hub.value(), lowPoints, lanes, 0, style);
    auto highEdge = network.addEdge(high.value(), hub.value(), highPoints, lanes, 0, style);
    auto exitEdge = network.addEdge(hub.value(), exit.value(), {P(0.f, 4.f, 0.f), P(24.f, 4.f, 0.f)},
                                    std::min(4, lanes + 1), 0, style);
    for (auto* edge : {&lowEdge, &highEdge, &exitEdge}) {
        if (!edge->ok()) return Result<RoadNetwork>::failure(edge->status());
    }
    auto turns = network.connectAllTurns(hub.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

}  // namespace

Result<void> RoadNetwork::validateStyle(const RoadStyle& style) const {
    auto profile = makeRoadProfile(style, 1, 0);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    if (!std::isfinite(style.speedLimitMps) || style.speedLimitMps <= 0.f || style.speedLimitMps > 200.f ||
        style.trafficPriority < 0 || style.trafficPriority > 255)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "road traffic settings are outside supported bounds",
                                                       "style.traffic"));
    if (!std::isfinite(style.sideObjectStartOffset) || style.sideObjectStartOffset < 0.f ||
        !std::isfinite(style.sideObjectEndOffset) || style.sideObjectEndOffset < 0.f)
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
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "node junction control is outside supported bounds", "node.junctionControl"));
    nodeIndex_[node.id] = static_cast<int>(nodes_.size());
    nodes_.push_back(node);
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
    if (from == to)
        return Result<std::uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge endpoints must be distinct", "edge"));
    if (controlPoints.size() < 2)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "edge needs at least two control points", "edge"));
    for (const auto& p : controlPoints) {
        if (!finite3(p.x, p.y, p.z))
            return Result<std::uint32_t>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "control points must be finite", "edge"));
    }
    auto styleOk = validateStyle(style);
    if (!styleOk.ok()) return Result<std::uint32_t>::failure(styleOk.status());
    auto profile = makeRoadProfile(style, lanesForward, lanesBackward);
    if (!profile.ok()) return Result<std::uint32_t>::failure(profile.status());

    // A node is the authoritative connection position. Canonicalizing the two
    // endpoint samples prevents authored handles from leaving invisible gaps or
    // sending turn curves toward a stale position.
    const auto& fromNode  = nodes_[static_cast<std::size_t>(findNodeIndex(from))];
    const auto& toNode    = nodes_[static_cast<std::size_t>(findNodeIndex(to))];
    controlPoints.front() = RoadControlPoint{fromNode.x, fromNode.y, fromNode.z};
    controlPoints.back()  = RoadControlPoint{toNode.x, toNode.y, toNode.z};
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
        if (!finite3(point.x, point.y, point.z))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "control points must be finite", "edge"));
    }
    auto styleOk = validateStyle(edge.style);
    if (!styleOk.ok()) return styleOk;
    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    const auto& from           = nodes_[static_cast<std::size_t>(findNodeIndex(edge.from))];
    const auto& to             = nodes_[static_cast<std::size_t>(findNodeIndex(edge.to))];
    edge.controlPoints.front() = RoadControlPoint{from.x, from.y, from.z};
    edge.controlPoints.back()  = RoadControlPoint{to.x, to.y, to.z};
    if (!hasUsableGeometry(edge.controlPoints))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge centerline must contain a non-degenerate segment", "edge"));
    edgeIndex_[edge.id]        = static_cast<int>(edges_.size());
    nextEdgeId_                = std::max(nextEdgeId_, edge.id + 1);
    edges_.push_back(std::move(edge));
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
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "lane link is persistently blocked", "laneLink"));
    laneLinks_.push_back(link);
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
    const auto active = std::find_if(candidate.laneLinks_.begin(), candidate.laneLinks_.end(),
                                     [&](const auto& current) { return sameLaneLink(current, link); });
    const bool removed = active != candidate.laneLinks_.end();
    if (removed) candidate.laneLinks_.erase(active);
    candidate.blockedLaneLinks_.push_back(link);
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
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "network contains an invalid junction node", "node"));
    }
    for (const auto& edge : edges_) {
        auto styleOk = validateStyle(edge.style);
        if (!styleOk.ok()) return styleOk;
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
    candidateNode.y = y;
    candidateNode.z = z;
    for (auto& edge : candidate.edges_) {
        if (edge.from == nodeId) edge.controlPoints.front() = RoadControlPoint{x, y, z};
        if (edge.to == nodeId) edge.controlPoints.back() = RoadControlPoint{x, y, z};
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
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "merging nodes would create a self-loop edge", "removeNodeId"));

    auto candidate = *this;
    int rewired = 0;
    for (auto& edge : candidate.edges_) {
        if (edge.from == removeNodeId) {
            edge.from = keepNodeId;
            edge.controlPoints.front() = {keep.x, keep.y, keep.z};
            ++rewired;
        }
        if (edge.to == removeNodeId) {
            edge.to = keepNodeId;
            edge.controlPoints.back() = {keep.x, keep.y, keep.z};
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
        if (!finite3(point.x, point.y, point.z))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "control points must be finite", "points"));
    }

    auto&       edge      = edges_[static_cast<std::size_t>(index)];
    const auto& from      = nodes_[static_cast<std::size_t>(findNodeIndex(edge.from))];
    const auto& to        = nodes_[static_cast<std::size_t>(findNodeIndex(edge.to))];
    controlPoints.front() = RoadControlPoint{from.x, from.y, from.z};
    controlPoints.back()  = RoadControlPoint{to.x, to.y, to.z};
    if (!hasUsableGeometry(controlPoints))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "edge centerline must contain a non-degenerate segment", "points"));
    edge.controlPoints    = std::move(controlPoints);
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
    auto& edge = candidate.edges_[static_cast<std::size_t>(index)];
    std::swap(edge.from, edge.to);
    std::reverse(edge.controlPoints.begin(), edge.controlPoints.end());
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
    auto valid = candidate.validate();
    if (!valid.ok()) return valid;
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return Result<void>::success();
}

Result<int> RoadNetwork::reconnectEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint,
                                               std::uint32_t nodeId) {
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
    const auto& node = candidate.nodes_[static_cast<std::size_t>(nodeIndex)];
    if (fromEndpoint) {
        edge.from = nodeId;
        edge.controlPoints.front() = {node.x, node.y, node.z};
    } else {
        edge.to = nodeId;
        edge.controlPoints.back() = {node.x, node.y, node.z};
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
    auto path = makeCenterlineSpline(original);
    if (!path.ok()) return Result<RoadEdgeSplitResult>::failure(path.status());
    auto evaluated = path.value().evaluateResult(parameter);
    if (!evaluated.ok()) return Result<RoadEdgeSplitResult>::failure(evaluated.status());

    const int segmentCount = path.value().segmentCount();
    const float scaled = parameter * static_cast<float>(segmentCount);
    const int nearest = static_cast<int>(std::round(scaled));
    auto candidate = *this;
    std::size_t splitIndex = 0;
    if (nearest > 0 && nearest < segmentCount && std::fabs(scaled - static_cast<float>(nearest)) <= 1e-5f) {
        splitIndex = static_cast<std::size_t>(nearest);
    } else {
        const int segment = std::clamp(static_cast<int>(std::floor(scaled)), 0, segmentCount - 1);
        auto points = original.controlPoints;
        splitIndex = static_cast<std::size_t>(segment + 1);
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(splitIndex),
                      RoadControlPoint{evaluated.value().x, evaluated.value().y, evaluated.value().z});
        auto changed = candidate.setEdgeControlPoints(edgeId, std::move(points));
        if (!changed.ok()) return Result<RoadEdgeSplitResult>::failure(changed.status());
    }
    auto split = candidate.splitEdge(edgeId, splitIndex, junctionRadius, nodeId, secondEdgeId);
    if (!split.ok()) return split;
    const auto result = split.value();
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
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

    auto candidate = *this;
    const auto splitPoint = original.controlPoints[controlPointIndex];
    std::uint32_t insertedNodeId = nodeId;
    if (insertedNodeId == 0) {
        auto node = candidate.addNode(splitPoint.x, splitPoint.y, splitPoint.z, junctionRadius);
        if (!node.ok()) return Result<RoadEdgeSplitResult>::failure(node.status());
        insertedNodeId = node.value();
    } else {
        auto node = candidate.restoreNode({insertedNodeId, splitPoint.x, splitPoint.y, splitPoint.z, junctionRadius});
        if (!node.ok()) return Result<RoadEdgeSplitResult>::failure(node.status());
    }

    auto& first = candidate.edges_[static_cast<std::size_t>(idx)];
    first.to = insertedNodeId;
    first.controlPoints.resize(controlPointIndex + 1u);
    first.controlPoints.back() = splitPoint;
    std::vector<RoadControlPoint> continuation(original.controlPoints.begin() +
                                                    static_cast<std::ptrdiff_t>(controlPointIndex),
                                                original.controlPoints.end());
    std::uint32_t continuationEdgeId = secondEdgeId;
    if (continuationEdgeId == 0) {
        auto second = candidate.addEdge(insertedNodeId, original.to, std::move(continuation), original.lanesForward,
                                        original.lanesBackward, original.style);
        if (!second.ok()) return Result<RoadEdgeSplitResult>::failure(second.status());
        continuationEdgeId = second.value();
    } else {
        auto second = candidate.restoreEdge({continuationEdgeId, insertedNodeId, original.to, std::move(continuation),
                                             original.lanesForward, original.lanesBackward, original.style});
        if (!second.ok()) return Result<RoadEdgeSplitResult>::failure(second.status());
    }

    // The original id remains attached to the `from` half. References whose
    // port lived at the old `to` endpoint migrate to the continuation edge.
    for (auto& link : candidate.laneLinks_) {
        if (link.inEdge == edgeId && link.inDirection == RoadLaneDirection::Forward)
            link.inEdge = continuationEdgeId;
        if (link.outEdge == edgeId && link.outDirection == RoadLaneDirection::Backward)
            link.outEdge = continuationEdgeId;
    }
    for (auto& link : candidate.blockedLaneLinks_) {
        if (link.inEdge == edgeId && link.inDirection == RoadLaneDirection::Forward)
            link.inEdge = continuationEdgeId;
        if (link.outEdge == edgeId && link.outDirection == RoadLaneDirection::Backward)
            link.outEdge = continuationEdgeId;
    }
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
    auto path = makeCenterlineSpline(edge);
    if (!path.ok()) return Result<RoadEdgeSplitResult>::failure(path.status());
    const int intervals = std::min(4096, std::max(32, path.value().segmentCount() * 32));
    float       bestDistanceSquared = std::numeric_limits<float>::max();
    float       bestParameter = 0.f;
    auto previous = path.value().evaluateResult(0.f);
    if (!previous.ok()) return Result<RoadEdgeSplitResult>::failure(previous.status());
    for (int i = 0; i < intervals; ++i) {
        const float nextParameter = static_cast<float>(i + 1) / static_cast<float>(intervals);
        auto next = path.value().evaluateResult(nextParameter);
        if (!next.ok()) return Result<RoadEdgeSplitResult>::failure(next.status());
        const RoadControlPoint a{previous.value().x, previous.value().y, previous.value().z};
        const RoadControlPoint b{next.value().x, next.value().y, next.value().z};
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float lengthSquared = dx * dx + dy * dy + dz * dz;
        if (lengthSquared > 1e-8f) {
            const float local = std::clamp(((position.x - a.x) * dx + (position.y - a.y) * dy +
                                            (position.z - a.z) * dz) /
                                               lengthSquared,
                                           0.f, 1.f);
            const RoadControlPoint projected{a.x + dx * local, a.y + dy * local, a.z + dz * local};
            const float px = position.x - projected.x, py = position.y - projected.y, pz = position.z - projected.z;
            const float distanceSquared = px * px + py * py + pz * pz;
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                const float previousParameter = static_cast<float>(i) / static_cast<float>(intervals);
                bestParameter = previousParameter + (nextParameter - previousParameter) * local;
            }
        }
        previous = std::move(next);
    }
    if (bestDistanceSquared > maxDistance * maxDistance)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "position is outside the road snap distance", "maxDistance"));

    constexpr float endpointTolerance = 1e-4f;
    if (bestParameter <= endpointTolerance || bestParameter >= 1.f - endpointTolerance)
        return Result<RoadEdgeSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "position resolves to an edge endpoint", "position"));
    return splitEdgeAtSplineParameter(edgeId, bestParameter, junctionRadius, nodeId, secondEdgeId);
}

Result<void> RoadNetwork::removeEdge(std::uint32_t edgeId) {
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

Result<RoadNetwork> RoadNetwork::makeStraight(float length, int lanes) {
    if (!std::isfinite(length) || length < 8.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "length>=8, lanes in [1,4] required", "straight"));
    RoadNetwork network;
    const float half = length * 0.5f;
    auto a = network.addNode(-half, 0.f, 0.f, 2.f);
    auto b = network.addNode(half, 0.f, 0.f, 2.f);
    if (!a.ok()) return Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return Result<RoadNetwork>::failure(b.status());
    auto edge = network.addEdge(a.value(), b.value(), {P(-half, 0.f, 0.f), P(0.f, 0.f, 0.f), P(half, 0.f, 0.f)}, lanes,
                                0, groundStyle());
    if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeCurve(float radius, int lanes) {
    if (!std::isfinite(radius) || radius < 8.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "radius>=8, lanes in [1,4] required", "curve"));
    RoadNetwork network;
    auto a = network.addNode(-radius, 0.f, 0.f, 2.f);
    auto b = network.addNode(0.f, 0.f, radius, 2.f);
    if (!a.ok()) return Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return Result<RoadNetwork>::failure(b.status());
    auto edge = network.addEdge(a.value(), b.value(),
                                {P(-radius, 0.f, 0.f), P(-radius * 0.55f, 0.f, radius * 0.15f),
                                 P(-radius * 0.15f, 0.f, radius * 0.55f), P(0.f, 0.f, radius)},
                                lanes, 0, groundStyle());
    if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeBridge(float length, float height, int lanes) {
    if (!std::isfinite(length) || length < 12.f || !std::isfinite(height) || height < 2.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "length>=12, height>=2, lanes in [1,4] required", "bridge"));
    RoadNetwork network;
    const float half = length * 0.5f;
    // Flat elevated deck at constant height (no ramps yet) — isolates pier baking
    // from Frenet-frame twist that shows up on steep climbs.
    auto a = network.addNode(-half, height, 0.f, 2.f);
    auto b = network.addNode(half, height, 0.f, 2.f);
    if (!a.ok()) return Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return Result<RoadNetwork>::failure(b.status());
    auto edge =
        network.addEdge(a.value(), b.value(), {P(-half, height, 0.f), P(0.f, height, 0.f), P(half, height, 0.f)}, lanes,
                        0, bridgeStyle());
    if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeCross(float span, int lanes) {
    if (!std::isfinite(span) || span < 16.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "span>=16, lanes in [1,4] required", "cross"));
    RoadNetwork network;
    const float half = span * 0.5f;
    RoadStyle style     = groundStyle();
    style.deckThickness = 0.08f;
    style.sidewalkWidth = 1.2f;
    style.curbWidth     = 0.35f;
    style.curbHeight    = 0.45f;
    // Push arms outward past the asphalt half-width so sidewalks do not overlap;
    // the leftover ring is filled by quarter-circle curb returns.
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    const float cornerR     = 2.8f;
    const float jr          = asphaltHalf + cornerR;
    auto nC = network.addNode(0.f, 0.f, 0.f, jr);
    auto nN = network.addNode(0.f, 0.f, -half, 2.f);
    auto nS = network.addNode(0.f, 0.f, half, 2.f);
    auto nW = network.addNode(-half, 0.f, 0.f, 2.f);
    auto nE = network.addNode(half, 0.f, 0.f, 2.f);
    for (auto* r : {&nC, &nN, &nS, &nW, &nE}) {
        if (!r->ok()) return Result<RoadNetwork>::failure(r->status());
    }
    // Arms run into the hub; bake trims each end by junctionRadius so strips
    // stop outside the corner arcs.
    auto e1 = network.addEdge(nN.value(), nC.value(), {P(0.f, 0.f, -half), P(0.f, 0.f, 0.f)}, lanes, 0, style);
    auto e2 = network.addEdge(nC.value(), nS.value(), {P(0.f, 0.f, 0.f), P(0.f, 0.f, half)}, lanes, 0, style);
    auto e3 = network.addEdge(nW.value(), nC.value(), {P(-half, 0.f, 0.f), P(0.f, 0.f, 0.f)}, lanes, 0, style);
    auto e4 = network.addEdge(nC.value(), nE.value(), {P(0.f, 0.f, 0.f), P(half, 0.f, 0.f)}, lanes, 0, style);
    for (auto* e : {&e1, &e2, &e3, &e4}) {
        if (!e->ok()) return Result<RoadNetwork>::failure(e->status());
    }
    auto turns = network.connectAllTurns(nC.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeTee(float span, int lanes) {
    if (!std::isfinite(span) || span < 16.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "span>=16, lanes in [1,4] required", "tee"));
    RoadNetwork network;
    const float half = span * 0.5f;
    RoadStyle  style = groundStyle();
    style.deckThickness = 0.08f;
    style.sidewalkWidth = 1.2f;
    style.curbWidth     = 0.35f;
    style.curbHeight    = 0.45f;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    const float jr          = asphaltHalf + 2.8f;
    auto        center      = network.addNode(0.f, 0.f, 0.f, jr);
    auto        south       = network.addNode(0.f, 0.f, half, 2.f);
    auto        west        = network.addNode(-half, 0.f, 0.f, 2.f);
    auto        east        = network.addNode(half, 0.f, 0.f, 2.f);
    for (auto* node : {&center, &south, &west, &east})
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    auto westEdge = network.addEdge(west.value(), center.value(),
                                    {P(-half, 0.f, 0.f), P(-half * 0.5f, 0.f, 0.f), P(0.f, 0.f, 0.f)}, lanes, 0,
                                    style);
    auto eastEdge = network.addEdge(center.value(), east.value(),
                                    {P(0.f, 0.f, 0.f), P(half * 0.5f, 0.f, 0.f), P(half, 0.f, 0.f)}, lanes, 0,
                                    style);
    auto southEdge = network.addEdge(center.value(), south.value(),
                                     {P(0.f, 0.f, 0.f), P(0.f, 0.f, half * 0.5f), P(0.f, 0.f, half)}, lanes, 0,
                                     style);
    for (auto* edge : {&westEdge, &eastEdge, &southEdge})
        if (!edge->ok()) return Result<RoadNetwork>::failure(edge->status());
    auto turns = network.connectAllTurns(center.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeY(float span, int lanes) {
    if (!std::isfinite(span) || span < 16.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "span>=16, lanes in [1,4] required", "y"));
    return makeFan(span, lanes, {270.f, 30.f, 150.f}, {true, false, false});
}

Result<RoadNetwork> RoadNetwork::makeFan(float span, int lanes, std::vector<float> armAnglesDeg,
                                         std::vector<bool> intoHub) {
    if (!std::isfinite(span) || span < 16.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "span>=16, lanes in [1,4] required", "fan"));
    if (armAnglesDeg.size() < 2u)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "fan needs at least two arm angles", "fan"));
    if (!intoHub.empty() && intoHub.size() != armAnglesDeg.size())
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "intoHub size must match armAnglesDeg", "fan"));

    struct ArmSpec {
        float degrees = 0.f;
        bool  intoHub = true;
    };
    std::vector<ArmSpec> specs;
    specs.reserve(armAnglesDeg.size());
    for (std::size_t index = 0; index < armAnglesDeg.size(); ++index) {
        if (!std::isfinite(armAnglesDeg[index]))
            return Result<RoadNetwork>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "arm angles must be finite", "fan"));
        float degrees = std::fmod(armAnglesDeg[index], 360.f);
        if (degrees < 0.f) degrees += 360.f;
        specs.push_back({degrees, intoHub.empty() || intoHub[index]});
    }
    std::sort(specs.begin(), specs.end(),
              [](const ArmSpec& first, const ArmSpec& second) { return first.degrees < second.degrees; });
    if (intoHub.empty())
        for (std::size_t index = 0; index < specs.size(); ++index) specs[index].intoHub = index % 2u == 0u;
    std::vector<ArmSpec> unique;
    unique.reserve(specs.size());
    for (const auto& spec : specs)
        if (unique.empty() || std::fabs(spec.degrees - unique.back().degrees) > 1.f) unique.push_back(spec);
    if (unique.size() >= 2u && std::fabs(unique.front().degrees + 360.f - unique.back().degrees) <= 1.f)
        unique.pop_back();
    if (unique.size() < 2u)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "fan needs at least two distinct arm angles", "fan"));

    RoadNetwork network;
    const float half = span * 0.5f;
    RoadStyle  style = groundStyle();
    style.deckThickness = 0.08f;
    style.sidewalkWidth = 1.2f;
    style.curbWidth     = 0.35f;
    style.curbHeight    = 0.45f;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    auto        center      = network.addNode(0.f, 0.f, 0.f, asphaltHalf + 2.8f);
    if (!center.ok()) return Result<RoadNetwork>::failure(center.status());
    for (const auto& spec : unique) {
        const float radians = spec.degrees * 0.01745329252f;
        const float x = half * std::cos(radians), z = half * std::sin(radians);
        auto leaf = network.addNode(x, 0.f, z, 2.f);
        if (!leaf.ok()) return Result<RoadNetwork>::failure(leaf.status());
        auto edge = spec.intoHub
                        ? network.addEdge(leaf.value(), center.value(),
                                          {P(x, 0.f, z), P(x * 0.5f, 0.f, z * 0.5f), P(0.f, 0.f, 0.f)}, lanes, 0,
                                          style)
                        : network.addEdge(center.value(), leaf.value(),
                                          {P(0.f, 0.f, 0.f), P(x * 0.5f, 0.f, z * 0.5f), P(x, 0.f, z)}, lanes, 0,
                                          style);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    auto turns = network.connectAllTurns(center.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeFork(float span, int lanes) {
    return makeFan(span, lanes, {60.f, 120.f, 270.f});
}

Result<RoadNetwork> RoadNetwork::makeSkew(float span, int lanes) {
    return makeFan(span, lanes, {0.f, 135.f, 270.f});
}

Result<RoadNetwork> RoadNetwork::makeRoundabout(float span, int lanes) {
    if (!std::isfinite(span) || span < 32.f || lanes < 1 || lanes > 3)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "span>=32, lanes in [1,3] required", "roundabout"));
    RoadNetwork network;
    const float outerRadius = span * 0.5f;
    const float ringRadius  = std::clamp(span * 0.25f, 8.f, 16.f);
    constexpr float pi        = 3.14159265358979323846f;
    constexpr int   ringCount = 8;
    constexpr float ringStep  = 2.f * pi / static_cast<float>(ringCount);
    std::vector<std::uint32_t> ringNodes;
    ringNodes.reserve(ringCount);
    for (int i = 0; i < ringCount; ++i) {
        const float angle = -0.5f * pi + static_cast<float>(i) * ringStep;
        const float seamRadius = (i % 2 == 0) ? 1.5f : 0.25f;
        auto node = network.addNode(std::cos(angle) * ringRadius, 0.f, std::sin(angle) * ringRadius, seamRadius);
        if (!node.ok()) return Result<RoadNetwork>::failure(node.status());
        ringNodes.push_back(node.value());
    }

    RoadStyle ringStyle       = groundStyle();
    ringStyle.laneWidth       = 3.25f;
    ringStyle.curbWidth       = 0.3f;
    ringStyle.sidewalkWidth   = 0.75f;
    ringStyle.trafficPriority = 10;
    RoadStyle approachStyle       = groundStyle();
    approachStyle.laneWidth       = 3.f;
    approachStyle.trafficPriority = 1;
    for (int i = 0; i < ringCount; ++i) {
        const float            a0     = -0.5f * pi + static_cast<float>(i) * ringStep;
        const float            a3     = a0 + ringStep;
        const float            handle = 2.f * ringRadius * std::sin(ringStep * 0.5f) / 3.f;
        const RoadControlPoint p0 = P(std::cos(a0) * ringRadius, 0.f, std::sin(a0) * ringRadius);
        const RoadControlPoint p3 = P(std::cos(a3) * ringRadius, 0.f, std::sin(a3) * ringRadius);
        const RoadControlPoint p1 = P(p0.x - std::sin(a0) * handle, 0.f, p0.z + std::cos(a0) * handle);
        const RoadControlPoint p2 = P(p3.x + std::sin(a3) * handle, 0.f, p3.z - std::cos(a3) * handle);
        auto edge = network.addEdge(ringNodes[static_cast<std::size_t>(i)],
                                    ringNodes[static_cast<std::size_t>((i + 1) % ringCount)],
                                    {p0, p1, p2, p3},
                                    lanes, 0, ringStyle);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    for (int i = 0; i < ringCount; i += 2) {
        const float angle = -0.5f * pi + static_cast<float>(i) * ringStep;
        auto outer = network.addNode(std::cos(angle) * outerRadius, 0.f, std::sin(angle) * outerRadius, 2.f);
        if (!outer.ok()) return Result<RoadNetwork>::failure(outer.status());
        auto approach = network.addEdge(outer.value(), ringNodes[static_cast<std::size_t>(i)], {{}, {}}, lanes, lanes,
                                        approachStyle);
        if (!approach.ok()) return Result<RoadNetwork>::failure(approach.status());
    }
    for (int i = 0; i < ringCount; i += 2) {
        const auto node = ringNodes[static_cast<std::size_t>(i)];
        auto control = network.setNodeJunctionControl(node, RoadJunctionControl::Yield);
        if (!control.ok()) return Result<RoadNetwork>::failure(control.status());
        auto connected = network.connectAllTurns(node);
        if (!connected.ok()) return Result<RoadNetwork>::failure(connected.status());
    }
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeScene(const std::string& scene, float span, float bridgeHeight, int lanes,
                                           std::uint32_t seed) {
    if (scene == "straight") return makeStraight(span, lanes);
    if (scene == "curve") return makeCurve(std::max(8.f, span * 0.5f), lanes);
    if (scene == "bridge") return makeBridge(span, bridgeHeight, lanes);
    if (scene == "cross") return makeCross(span, lanes);
    if (scene == "tee" || scene == "t") return makeTee(span, lanes);
    if (scene == "y") return makeY(span, lanes);
    if (scene == "fork") return makeFork(span, lanes);
    if (scene == "skew") return makeSkew(span, lanes);
    if (scene == "t-junction" || scene == "y-junction") return makeThreeArmScene(scene, lanes);
    if (scene == "sloped-t") return makeSlopedJunctionScene(lanes, false);
    if (scene == "curve-uphill") return makeSlopedJunctionScene(lanes, true);
    if (scene == "tight-turn") return makeTightTurnScene(lanes);
    if (scene == "roundabout") return makeRoundabout(span, std::min(lanes, 3));
    if (scene == "interchange" || scene.empty()) return makeInterchange(span, bridgeHeight, lanes, seed);
    return Result<RoadNetwork>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument,
        "scene must be straight|curve|bridge|cross|tee|t|y|fork|skew|t-junction|y-junction|sloped-t|curve-uphill|tight-turn|roundabout|interchange",
        "scene"));
}

Result<RoadNetwork> RoadNetwork::makeInterchange(float span, float bridgeHeight, int lanes, std::uint32_t seed) {
    if (!std::isfinite(span) || span < 16.f || !std::isfinite(bridgeHeight) || bridgeHeight < 1.f || lanes < 1 ||
        lanes > 4)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "span>=16, bridgeHeight>=1, lanes in [1,4] required", "interchange"));

    RoadNetwork network;
    const float half = span * 0.5f;
    const float h    = bridgeHeight;
    // Tiny seed wobble on elevated mid-handles only — ground cross stays axis-aligned
    // so bakeJunction keeps the same outward-center curb returns as makeCross.
    const float wobble = 0.25f * static_cast<float>(static_cast<int>(seed % 7u) - 3);

    RoadStyle   ground      = groundStyle();
    ground.deckThickness    = 0.08f;
    ground.sidewalkWidth    = 1.2f;
    ground.curbWidth        = 0.35f;
    ground.curbHeight       = 0.45f;
    const float asphaltHalf = 0.5f * ground.laneWidth * static_cast<float>(lanes);
    const float cornerR     = 2.8f;
    const float jr          = asphaltHalf + cornerR;

    RoadStyle bridge     = bridgeStyle();
    bridge.pierSpacing   = 14.0f;
    bridge.sidewalkWidth = 1.0f;
    bridge.curbWidth     = 0.38f;
    bridge.curbHeight    = 0.50f;
    RoadStyle ramp       = bridge;
    ramp.sidewalkWidth   = 0.5f;
    ramp.pierSpacing     = 10.f;

    auto nGround = network.addNode(0.f, 0.f, 0.f, jr);
    auto nN      = network.addNode(0.f, 0.f, -half, 2.f);
    auto nS      = network.addNode(0.f, 0.f, half, 2.f);
    auto nW      = network.addNode(-half, 0.f, 0.f, 2.f);
    auto nE      = network.addNode(half, 0.f, 0.f, 2.f);

    // The elevated diagonal crosses the ground graph in XZ but has its own node
    // at bridge height. Equal planar coordinates never imply connectivity.
    auto nBridgeNW = network.addNode(-half, h, -half, 2.0f);
    auto nElev     = network.addNode(0.f, h, 0.f, 2.0f);
    auto nBridgeSE = network.addNode(half, h, half, 2.0f);
    for (auto* res : {&nGround, &nN, &nS, &nW, &nE, &nBridgeNW, &nElev, &nBridgeSE}) {
        if (!res->ok()) return Result<RoadNetwork>::failure(res->status());
    }

    auto add = [&](std::uint32_t a, std::uint32_t b, std::vector<RoadControlPoint> pts, const RoadStyle& style) {
        return network.addEdge(a, b, std::move(pts), lanes, 0, style);
    };

    // Ground cross — same fillet construction as makeCross.
    const auto g  = nGround.value();
    auto       e1 = add(nN.value(), g, {P(0.f, 0.f, -half), P(0.f, 0.f, 0.f)}, ground);
    auto       e2 = add(g, nS.value(), {P(0.f, 0.f, 0.f), P(0.f, 0.f, half)}, ground);
    auto       e3 = add(nW.value(), g, {P(-half, 0.f, 0.f), P(0.f, 0.f, 0.f)}, ground);
    auto       e4 = add(g, nE.value(), {P(0.f, 0.f, 0.f), P(half, 0.f, 0.f)}, ground);

    // The central split is a smooth continuation and therefore creates no
    // junction apron. Seed wobble remains bounded and tangent-continuous.
    auto e5 = add(nBridgeNW.value(), nElev.value(),
                  {P(-half, h, -half), P(-half * 0.45f + wobble, h, -half * 0.45f), P(0.f, h, 0.f)}, bridge);
    auto e6 = add(nElev.value(), nBridgeSE.value(),
                  {P(0.f, h, 0.f), P(half * 0.45f, h, half * 0.45f + wobble), P(half, h, half)}, bridge);

    // Four outer ramps connect both ground approaches to the elevated diagonal
    // without inventing planar connectivity at the central grade separation.
    auto r1 = network.addEdge(nN.value(), nBridgeNW.value(),
                              {P(0.f, 0.f, -half), P(-half * 0.35f, h * 0.2f, -half),
                               P(-half * 0.75f, h * 0.75f, -half), P(-half, h, -half)},
                              1, 1, ramp);
    auto r2 = network.addEdge(nW.value(), nBridgeNW.value(),
                              {P(-half, 0.f, 0.f), P(-half, h * 0.2f, -half * 0.35f),
                               P(-half, h * 0.75f, -half * 0.75f), P(-half, h, -half)},
                              1, 1, ramp);
    auto r3 = network.addEdge(nBridgeSE.value(), nS.value(),
                              {P(half, h, half), P(half * 0.75f, h * 0.75f, half),
                               P(half * 0.35f, h * 0.2f, half), P(0.f, 0.f, half)},
                              1, 1, ramp);
    auto r4 = network.addEdge(nBridgeSE.value(), nE.value(),
                              {P(half, h, half), P(half, h * 0.75f, half * 0.75f),
                               P(half, h * 0.2f, half * 0.35f), P(half, 0.f, 0.f)},
                              1, 1, ramp);

    for (auto* edge : {&e1, &e2, &e3, &e4, &e5, &e6, &r1, &r2, &r3, &r4}) {
        if (!edge->ok()) return Result<RoadNetwork>::failure(edge->status());
    }

    // Route links exist only at the ground intersection. The elevated road is
    // a separate graph despite sharing the same XZ crossing coordinate.
    auto turns = network.connectAllTurns(g);
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    for (const auto node : {nN.value(), nW.value(), nBridgeNW.value(), nBridgeSE.value(), nS.value(), nE.value()}) {
        turns = network.connectAllTurns(node);
        if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    }

    return Result<RoadNetwork>::success(std::move(network));
}

}  // namespace eve::procgen::road
