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
RoadControlPoint P(float x, float y, float z) { return RoadControlPoint{x, y, z}; }
RoadStyle        groundStyle() {
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
    RoadStyle   style   = groundStyle();
    style.laneWidth     = 1.2f;
    style.sidewalkWidth = 0.25f;

    auto incomingStart = network.addNode(-9.f, 0.f, -3.f, 1.f);
    auto hub           = network.addNode(0.f, 0.f, 0.f, 8.f);
    auto outgoingEnd   = network.addNode(-9.f, 0.f, 3.f, 1.f);
    for (auto* node : {&incomingStart, &hub, &outgoingEnd}) {
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    }

    auto incoming =
        network.addEdge(incomingStart.value(), hub.value(), {P(-9.f, 0.f, -3.f), P(0.f, 0.f, 0.f)}, lanes, 0, style);
    auto outgoing =
        network.addEdge(hub.value(), outgoingEnd.value(), {P(0.f, 0.f, 0.f), P(-9.f, 0.f, 3.f)}, lanes, 0, style);
    if (!incoming.ok()) return Result<RoadNetwork>::failure(incoming.status());
    if (!outgoing.ok()) return Result<RoadNetwork>::failure(outgoing.status());
    auto turn = network.addLaneLink(
        {incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward});
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
    auto       hub       = network.addNode(0.f, 0.f, 0.f, yJunction ? 5.f : 6.f);
    auto       stem      = network.addNode(0.f, 0.f, 24.f, 2.f);
    auto       left      = network.addNode(-18.f, 0.f, yJunction ? -18.f : 0.f, 2.f);
    auto       right     = network.addNode(18.f, 0.f, yJunction ? -18.f : 0.f, 2.f);
    for (auto* node : {&hub, &stem, &left, &right}) {
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    }
    auto incoming = network.addEdge(stem.value(), hub.value(), {P(0.f, 0.f, 24.f), P(0.f, 0.f, 0.f)}, lanes,
                                    yJunction && lanes > 1 ? 1 : 0, yJunction ? wide : narrow);
    auto west = network.addEdge(hub.value(), left.value(), {P(0.f, 0.f, 0.f), P(-18.f, 0.f, yJunction ? -18.f : 0.f)},
                                lanes, 0, narrow);
    auto east = network.addEdge(hub.value(), right.value(), {P(0.f, 0.f, 0.f), P(18.f, 0.f, yJunction ? -18.f : 0.f)},
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
    auto        hub   = network.addNode(0.f, 4.f, 0.f, 6.f);
    auto        low   = network.addNode(-22.f, 0.f, 12.f, 2.f);
    auto        high  = network.addNode(-20.f, 7.f, -14.f, 2.f);
    auto        exit  = network.addNode(24.f, 4.f, 0.f, 2.f);
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

Result<RoadNetwork> RoadNetwork::makeStraight(float length, int lanes) {
    if (!std::isfinite(length) || length < 8.f || lanes < 1 || lanes > 4)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "length>=8, lanes in [1,4] required", "straight"));
    RoadNetwork network;
    const float half = length * 0.5f;
    auto        a    = network.addNode(-half, 0.f, 0.f, 2.f);
    auto        b    = network.addNode(half, 0.f, 0.f, 2.f);
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
    auto        a = network.addNode(-radius, 0.f, 0.f, 2.f);
    auto        b = network.addNode(0.f, 0.f, radius, 2.f);
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
    const float half    = span * 0.5f;
    RoadStyle   style   = groundStyle();
    style.deckThickness = 0.08f;
    style.sidewalkWidth = 1.2f;
    style.curbWidth     = 0.35f;
    style.curbHeight    = 0.45f;
    // Push arms outward past the asphalt half-width so sidewalks do not overlap;
    // the leftover ring is filled by quarter-circle curb returns.
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    const float cornerR     = 2.8f;
    const float jr          = asphaltHalf + cornerR;
    auto        nC          = network.addNode(0.f, 0.f, 0.f, jr);
    auto        nN          = network.addNode(0.f, 0.f, -half, 2.f);
    auto        nS          = network.addNode(0.f, 0.f, half, 2.f);
    auto        nW          = network.addNode(-half, 0.f, 0.f, 2.f);
    auto        nE          = network.addNode(half, 0.f, 0.f, 2.f);
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
    const float half        = span * 0.5f;
    RoadStyle   style       = groundStyle();
    style.deckThickness     = 0.08f;
    style.sidewalkWidth     = 1.2f;
    style.curbWidth         = 0.35f;
    style.curbHeight        = 0.45f;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    const float jr          = asphaltHalf + 2.8f;
    auto        center      = network.addNode(0.f, 0.f, 0.f, jr);
    auto        south       = network.addNode(0.f, 0.f, half, 2.f);
    auto        west        = network.addNode(-half, 0.f, 0.f, 2.f);
    auto        east        = network.addNode(half, 0.f, 0.f, 2.f);
    for (auto* node : {&center, &south, &west, &east})
        if (!node->ok()) return Result<RoadNetwork>::failure(node->status());
    auto westEdge  = network.addEdge(west.value(), center.value(),
                                     {P(-half, 0.f, 0.f), P(-half * 0.5f, 0.f, 0.f), P(0.f, 0.f, 0.f)}, lanes, 0, style);
    auto eastEdge  = network.addEdge(center.value(), east.value(),
                                     {P(0.f, 0.f, 0.f), P(half * 0.5f, 0.f, 0.f), P(half, 0.f, 0.f)}, lanes, 0, style);
    auto southEdge = network.addEdge(center.value(), south.value(),
                                     {P(0.f, 0.f, 0.f), P(0.f, 0.f, half * 0.5f), P(0.f, 0.f, half)}, lanes, 0, style);
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
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "intoHub size must match armAnglesDeg", "fan"));

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
    const float half        = span * 0.5f;
    RoadStyle   style       = groundStyle();
    style.deckThickness     = 0.08f;
    style.sidewalkWidth     = 1.2f;
    style.curbWidth         = 0.35f;
    style.curbHeight        = 0.45f;
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanes);
    auto        center      = network.addNode(0.f, 0.f, 0.f, asphaltHalf + 2.8f);
    if (!center.ok()) return Result<RoadNetwork>::failure(center.status());
    for (const auto& spec : unique) {
        const float radians = spec.degrees * 0.01745329252f;
        const float x = half * std::cos(radians), z = half * std::sin(radians);
        auto        leaf = network.addNode(x, 0.f, z, 2.f);
        if (!leaf.ok()) return Result<RoadNetwork>::failure(leaf.status());
        auto edge =
            spec.intoHub
                ? network.addEdge(leaf.value(), center.value(),
                                  {P(x, 0.f, z), P(x * 0.5f, 0.f, z * 0.5f), P(0.f, 0.f, 0.f)}, lanes, 0, style)
                : network.addEdge(center.value(), leaf.value(),
                                  {P(0.f, 0.f, 0.f), P(x * 0.5f, 0.f, z * 0.5f), P(x, 0.f, z)}, lanes, 0, style);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    auto turns = network.connectAllTurns(center.value());
    if (!turns.ok()) return Result<RoadNetwork>::failure(turns.status());
    return Result<RoadNetwork>::success(std::move(network));
}

Result<RoadNetwork> RoadNetwork::makeFork(float span, int lanes) { return makeFan(span, lanes, {60.f, 120.f, 270.f}); }

Result<RoadNetwork> RoadNetwork::makeSkew(float span, int lanes) { return makeFan(span, lanes, {0.f, 135.f, 270.f}); }

Result<RoadNetwork> RoadNetwork::makeRoundabout(float span, int lanes) {
    if (!std::isfinite(span) || span < 32.f || lanes < 1 || lanes > 3)
        return Result<RoadNetwork>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "span>=32, lanes in [1,3] required", "roundabout"));
    RoadNetwork                network;
    const float                outerRadius = span * 0.5f;
    const float                ringRadius  = std::clamp(span * 0.25f, 8.f, 16.f);
    constexpr float            pi          = 3.14159265358979323846f;
    constexpr int              ringCount   = 8;
    constexpr float            ringStep    = 2.f * pi / static_cast<float>(ringCount);
    std::vector<std::uint32_t> ringNodes;
    ringNodes.reserve(ringCount);
    for (int i = 0; i < ringCount; ++i) {
        const float angle      = -0.5f * pi + static_cast<float>(i) * ringStep;
        const float seamRadius = (i % 2 == 0) ? 1.5f : 0.25f;
        auto        node = network.addNode(std::cos(angle) * ringRadius, 0.f, std::sin(angle) * ringRadius, seamRadius);
        if (!node.ok()) return Result<RoadNetwork>::failure(node.status());
        ringNodes.push_back(node.value());
    }

    RoadStyle ringStyle           = groundStyle();
    ringStyle.laneWidth           = 3.25f;
    ringStyle.curbWidth           = 0.3f;
    ringStyle.sidewalkWidth       = 0.75f;
    ringStyle.trafficPriority     = 10;
    RoadStyle approachStyle       = groundStyle();
    approachStyle.laneWidth       = 3.f;
    approachStyle.trafficPriority = 1;
    for (int i = 0; i < ringCount; ++i) {
        const float            a0     = -0.5f * pi + static_cast<float>(i) * ringStep;
        const float            a3     = a0 + ringStep;
        const float            handle = 2.f * ringRadius * std::sin(ringStep * 0.5f) / 3.f;
        const RoadControlPoint p0     = P(std::cos(a0) * ringRadius, 0.f, std::sin(a0) * ringRadius);
        const RoadControlPoint p3     = P(std::cos(a3) * ringRadius, 0.f, std::sin(a3) * ringRadius);
        const RoadControlPoint p1     = P(p0.x - std::sin(a0) * handle, 0.f, p0.z + std::cos(a0) * handle);
        const RoadControlPoint p2     = P(p3.x + std::sin(a3) * handle, 0.f, p3.z - std::cos(a3) * handle);
        auto                   edge   = network.addEdge(ringNodes[static_cast<std::size_t>(i)],
                                                        ringNodes[static_cast<std::size_t>((i + 1) % ringCount)], {p0, p1, p2, p3}, lanes,
                                                        0, ringStyle);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    for (int i = 0; i < ringCount; i += 2) {
        const float angle = -0.5f * pi + static_cast<float>(i) * ringStep;
        auto        outer = network.addNode(std::cos(angle) * outerRadius, 0.f, std::sin(angle) * outerRadius, 2.f);
        if (!outer.ok()) return Result<RoadNetwork>::failure(outer.status());
        auto approach = network.addEdge(outer.value(), ringNodes[static_cast<std::size_t>(i)], {{}, {}}, lanes, lanes,
                                        approachStyle);
        if (!approach.ok()) return Result<RoadNetwork>::failure(approach.status());
    }
    for (int i = 0; i < ringCount; i += 2) {
        const auto node    = ringNodes[static_cast<std::size_t>(i)];
        auto       control = network.setNodeJunctionControl(node, RoadJunctionControl::Yield);
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
    return Result<RoadNetwork>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument,
                          "scene must be "
                          "straight|curve|bridge|cross|tee|t|y|fork|skew|t-junction|y-junction|sloped-t|curve-uphill|"
                          "tight-turn|roundabout|interchange",
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

    RoadStyle ground        = groundStyle();
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
    auto r1 = network.addEdge(
        nN.value(), nBridgeNW.value(),
        {P(0.f, 0.f, -half), P(-half * 0.35f, h * 0.2f, -half), P(-half * 0.75f, h * 0.75f, -half), P(-half, h, -half)},
        1, 1, ramp);
    auto r2 = network.addEdge(
        nW.value(), nBridgeNW.value(),
        {P(-half, 0.f, 0.f), P(-half, h * 0.2f, -half * 0.35f), P(-half, h * 0.75f, -half * 0.75f), P(-half, h, -half)},
        1, 1, ramp);
    auto r3 = network.addEdge(
        nBridgeSE.value(), nS.value(),
        {P(half, h, half), P(half * 0.75f, h * 0.75f, half), P(half * 0.35f, h * 0.2f, half), P(0.f, 0.f, half)}, 1, 1,
        ramp);
    auto r4 = network.addEdge(
        nBridgeSE.value(), nE.value(),
        {P(half, h, half), P(half, h * 0.75f, half * 0.75f), P(half, h * 0.2f, half * 0.35f), P(half, 0.f, 0.f)}, 1, 1,
        ramp);

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
