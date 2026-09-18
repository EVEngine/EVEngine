#include "procgen/road/RoadNetwork.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace eve::procgen::road {
namespace {

bool finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

RoadControlPoint P(float x, float y, float z) { return RoadControlPoint{x, y, z}; }

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

}  // namespace

Result<void> RoadNetwork::validateStyle(const RoadStyle& style) const {
    auto profile = makeRoadProfile(style, 1, 0);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    return Result<void>::success();
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

Result<std::uint32_t> RoadNetwork::addEdge(std::uint32_t from, std::uint32_t to,
                                           std::vector<RoadControlPoint> controlPoints, int lanesForward,
                                           int lanesBackward, RoadStyle style) {
    if (findNodeIndex(from) < 0 || findNodeIndex(to) < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "edge endpoints must reference live nodes", "edge"));
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

Result<void> RoadNetwork::addLaneLink(RoadLaneLink link) {
    const int inIdx  = findEdgeIndex(link.inEdge);
    const int outIdx = findEdgeIndex(link.outEdge);
    if (inIdx < 0 || outIdx < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "lane link references missing edges", "laneLink"));
    const auto& inEdge  = edges_[static_cast<std::size_t>(inIdx)];
    const auto& outEdge = edges_[static_cast<std::size_t>(outIdx)];
    if (inEdge.to != outEdge.from)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                      "lane link requires inEdge.to == outEdge.from", "laneLink"));
    if (link.inLane < 0 || link.inLane >= inEdge.lanesForward)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "inLane out of range", "laneLink"));
    if (link.outLane < 0 || link.outLane >= outEdge.lanesForward)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "outLane out of range", "laneLink"));
    laneLinks_.push_back(link);
    ++revision_;
    return Result<void>::success();
}

Result<int> RoadNetwork::connectAllTurns(std::uint32_t nodeId) {
    if (findNodeIndex(nodeId) < 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "nodeId"));
    int added = 0;
    for (const auto& inEdge : edges_) {
        if (inEdge.to != nodeId) continue;
        for (const auto& outEdge : edges_) {
            if (outEdge.from != nodeId) continue;
            if (outEdge.id == inEdge.id) continue;
            for (int il = 0; il < inEdge.lanesForward; ++il) {
                const int ol = std::min(il, outEdge.lanesForward - 1);
                auto r = addLaneLink(RoadLaneLink{inEdge.id, il, outEdge.id, ol});
                if (!r.ok()) return Result<int>::failure(r.status());
                ++added;
            }
        }
    }
    return Result<int>::success(added);
}

Result<void> RoadNetwork::setEdgeStyle(std::uint32_t edgeId, RoadStyle style) {
    const int idx = findEdgeIndex(edgeId);
    if (idx < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "edgeId"));
    auto styleOk = validateStyle(style);
    if (!styleOk.ok()) return styleOk;
    auto profile = makeRoadProfile(style, edges_[static_cast<std::size_t>(idx)].lanesForward,
                                   edges_[static_cast<std::size_t>(idx)].lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    edges_[static_cast<std::size_t>(idx)].style = style;
    ++revision_;
    return Result<void>::success();
}

void RoadNetwork::clear() {
    nodes_.clear();
    edges_.clear();
    laneLinks_.clear();
    nodeIndex_.clear();
    edgeIndex_.clear();
    nextNodeId_ = 1;
    nextEdgeId_ = 1;
    ++revision_;
}

Result<RoadNode> RoadNetwork::nodeResult(std::uint32_t id) const {
    const int idx = findNodeIndex(id);
    if (idx < 0)
        return Result<RoadNode>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown node", "id"));
    return Result<RoadNode>::success(nodes_[static_cast<std::size_t>(idx)]);
}

Result<RoadEdge> RoadNetwork::edgeResult(std::uint32_t id) const {
    const int idx = findEdgeIndex(id);
    if (idx < 0)
        return Result<RoadEdge>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown edge", "id"));
    return Result<RoadEdge>::success(edges_[static_cast<std::size_t>(idx)]);
}

Result<RoadNetwork> RoadNetwork::makeStraight(float length, int lanes) {
    if (!std::isfinite(length) || length < 8.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "length>=8, lanes in [1,4] required", "straight"));
    RoadNetwork network;
    const float half = length * 0.5f;
    auto a = network.addNode(-half, 0.f, 0.f, 2.f);
    auto b = network.addNode(half, 0.f, 0.f, 2.f);
    if (!a.ok()) return Result<RoadNetwork>::failure(a.status());
    if (!b.ok()) return Result<RoadNetwork>::failure(b.status());
    auto edge = network.addEdge(a.value(), b.value(), {P(-half, 0.f, 0.f), P(0.f, 0.f, 0.f), P(half, 0.f, 0.f)},
                                lanes, 0, groundStyle());
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
    auto edge = network.addEdge(a.value(), b.value(),
                                {P(-half, height, 0.f), P(0.f, height, 0.f), P(half, height, 0.f)}, lanes, 0,
                                bridgeStyle());
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

Result<RoadNetwork> RoadNetwork::makeScene(const std::string& scene, float span, float bridgeHeight, int lanes,
                                           std::uint32_t seed) {
    if (scene == "straight") return makeStraight(span, lanes);
    if (scene == "curve") return makeCurve(std::max(8.f, span * 0.5f), lanes);
    if (scene == "bridge") return makeBridge(span, bridgeHeight, lanes);
    if (scene == "cross") return makeCross(span, lanes);
    if (scene == "interchange" || scene.empty()) return makeInterchange(span, bridgeHeight, lanes, seed);
    return Result<RoadNetwork>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "scene must be straight|curve|bridge|cross|interchange", "scene"));
}

Result<RoadNetwork> RoadNetwork::makeInterchange(float span, float bridgeHeight, int lanes, std::uint32_t seed) {
    if (!std::isfinite(span) || span < 16.f || !std::isfinite(bridgeHeight) || bridgeHeight < 1.f || lanes < 1 ||
        lanes > 4)
        return Result<RoadNetwork>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "span>=16, bridgeHeight>=1, lanes in [1,4] required", "interchange"));

    RoadNetwork network;
    const float half = span * 0.5f;
    const float h    = bridgeHeight;
    // Small seed-driven wobble keeps recipe deterministic without looking rigid.
    const float wobble = 0.35f * static_cast<float>((seed % 7u) - 3);

    auto nGround = network.addNode(0.f, 0.f, 0.f, 7.5f);
    auto nN      = network.addNode(0.f, 0.f, -half, 5.f);
    auto nS      = network.addNode(0.f, 0.f, half, 5.f);
    auto nW      = network.addNode(-half, 0.f, 0.f, 5.f);
    auto nE      = network.addNode(half, 0.f, 0.f, 5.f);
    auto nElev   = network.addNode(0.f, h, 0.f, 6.5f);
    auto nNE     = network.addNode(half * 0.55f, h, -half * 0.55f, 4.5f);
    auto nSE     = network.addNode(half * 0.55f, h, half * 0.55f, 4.5f);
    auto nSW     = network.addNode(-half * 0.55f, h, half * 0.55f, 4.5f);
    auto nNW     = network.addNode(-half * 0.55f, h, -half * 0.55f, 4.5f);
    for (auto* r : {&nGround, &nN, &nS, &nW, &nE, &nElev, &nNE, &nSE, &nSW, &nNW}) {
        if (!r->ok()) return Result<RoadNetwork>::failure(r->status());
    }

    RoadStyle ground       = groundStyle();
    RoadStyle bridge       = bridgeStyle();
    bridge.pierSpacing     = 9.5f + 0.2f * wobble;

    const auto g = nGround.value();
    auto add = [&](std::uint32_t a, std::uint32_t b, std::vector<RoadControlPoint> pts, const RoadStyle& style) {
        return network.addEdge(a, b, std::move(pts), lanes, 0, style);
    };

    // Ground cross.
    auto e1 = add(nN.value(), g, {P(0, 0, -half), P(wobble, 0, -half * 0.45f), P(0, 0, -7.5f)}, ground);
    auto e2 = add(g, nS.value(), {P(0, 0, 7.5f), P(-wobble, 0, half * 0.45f), P(0, 0, half)}, ground);
    auto e3 = add(nW.value(), g, {P(-half, 0, 0), P(-half * 0.45f, 0, wobble), P(-7.5f, 0, 0)}, ground);
    auto e4 = add(g, nE.value(), {P(7.5f, 0, 0), P(half * 0.45f, 0, -wobble), P(half, 0, 0)}, ground);

    // Elevated ring.
    const float r = half * 0.55f;
    auto e5 =
        add(nNW.value(), nNE.value(),
            {P(-r, h, -r), P(-r * 0.2f + wobble, h, -r * 1.05f), P(r * 0.2f, h, -r * 1.05f), P(r, h, -r)}, bridge);
    auto e6 =
        add(nNE.value(), nSE.value(),
            {P(r, h, -r), P(r * 1.05f, h, -r * 0.2f), P(r * 1.05f, h, r * 0.2f + wobble), P(r, h, r)}, bridge);
    auto e7 =
        add(nSE.value(), nSW.value(),
            {P(r, h, r), P(r * 0.2f, h, r * 1.05f), P(-r * 0.2f - wobble, h, r * 1.05f), P(-r, h, r)}, bridge);
    auto e8 =
        add(nSW.value(), nNW.value(),
            {P(-r, h, r), P(-r * 1.05f, h, r * 0.2f), P(-r * 1.05f, h, -r * 0.2f - wobble), P(-r, h, -r)}, bridge);

    // Overpass through elevated hub.
    auto e9 = add(nNW.value(), nElev.value(),
                  {P(-r, h, -r), P(-r * 0.35f, h, -r * 0.35f), P(-6.f, h, -6.f)}, bridge);
    auto e10 =
        add(nElev.value(), nSE.value(), {P(6.f, h, 6.f), P(r * 0.35f, h, r * 0.35f), P(r, h, r)}, bridge);

    // Ramps: ground east → elevated SE, elevated NW → ground west.
    auto e11 = add(nE.value(), nSE.value(),
                   {P(half, 0, 0), P(half * 0.75f, h * 0.35f, half * 0.2f), P(half * 0.6f, h * 0.75f, half * 0.4f),
                    P(r, h, r)},
                   bridge);
    auto e12 = add(nNW.value(), nW.value(),
                   {P(-r, h, -r), P(-half * 0.6f, h * 0.75f, -half * 0.35f),
                    P(-half * 0.75f, h * 0.35f, -half * 0.15f), P(-half, 0, 0)},
                   bridge);

    for (auto* rEdge : {&e1, &e2, &e3, &e4, &e5, &e6, &e7, &e8, &e9, &e10, &e11, &e12}) {
        if (!rEdge->ok()) return Result<RoadNetwork>::failure(rEdge->status());
    }

    for (std::uint32_t node : {g, nElev.value(), nNE.value(), nSE.value(), nSW.value(), nNW.value(), nE.value(),
                               nW.value()}) {
        auto turns = network.connectAllTurns(node);
        if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    }

    return Result<RoadNetwork>::success(std::move(network));
}

}  // namespace eve::procgen::road
