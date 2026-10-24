#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"

#include "common/Diagnostic.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::procgen::road::detail {

Result<void> bakeTurnOverlays(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network,
                              const RoadBakeOptions&                   options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes) {
    std::unordered_map<std::uint32_t, SplinePath> paths;
    for (const auto& edge : network.edges()) {
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) return Result<void>::failure(spline.status());
        paths.emplace(edge.id, std::move(spline).takeValue());
    }

    for (const auto& link : network.laneLinks()) {
        auto inEdge  = network.edgeResult(link.inEdge);
        auto outEdge = network.edgeResult(link.outEdge);
        if (!inEdge.ok() || !outEdge.ok()) continue;
        const std::uint32_t nodeId =
            link.inDirection == RoadLaneDirection::Forward ? inEdge.value().to : inEdge.value().from;
        auto node = network.nodeResult(nodeId);
        if (!node.ok()) continue;

        auto& inPath  = paths[link.inEdge];
        auto& outPath = paths[link.outEdge];
        auto  inLen   = inPath.lengthResult(16);
        auto  outLen  = outPath.lengthResult(16);
        if (!inLen.ok() || !outLen.ok()) continue;

        const bool  activeJunction = activeJunctionNodes.contains(node.value().id);
        const float inTrim =
            activeJunction ? junctionSocketDistance(network, node.value(), inEdge.value(), inLen.value()) : 0.f;
        const float outTrim =
            activeJunction ? junctionSocketDistance(network, node.value(), outEdge.value(), outLen.value()) : 0.f;
        const float inDist   = link.inDirection == RoadLaneDirection::Forward ? inLen.value() - inTrim : inTrim;
        const float outDist  = link.outDirection == RoadLaneDirection::Forward ? outTrim : outLen.value() - outTrim;
        auto        inFrame  = inPath.travelFrameResult(inDist, "clamp", 16);
        auto        outFrame = outPath.travelFrameResult(outDist, "clamp", 16);
        if (!inFrame.ok() || !outFrame.ok()) continue;
        if (activeJunction) {
            const JunctionPlane plane = fitJunctionPlane(network, node.value());
            blendFrameToJunctionPlane(inFrame.value(), plane, 1.f);
            blendFrameToJunctionPlane(outFrame.value(), plane, 1.f);
        }

        const float inLat  = laneCenterOffset(inEdge.value().style, inEdge.value().lanesForward,
                                              inEdge.value().lanesBackward, link.inLane, link.inDirection);
        const float outLat = laneCenterOffset(outEdge.value().style, outEdge.value().lanesForward,
                                              outEdge.value().lanesBackward, link.outLane, link.outDirection);

        const auto& fi = inFrame.value();
        const auto& fo = outFrame.value();
        const V3    p0{fi.sample.x, fi.sample.y, fi.sample.z};
        const V3    p1{fo.sample.x, fo.sample.y, fo.sample.z};
        const V3    si{fi.sideX, fi.sideY, fi.sideZ};
        const V3    so{fo.sideX, fo.sideY, fo.sideZ};
        const V3    ui{fi.upX, fi.upY, fi.upZ};
        const V3    uo{fo.upX, fo.upY, fo.upZ};
        V3          ti{fi.forwardX, fi.forwardY, fi.forwardZ};
        V3          to{fo.forwardX, fo.forwardY, fo.forwardZ};
        if (link.inDirection == RoadLaneDirection::Backward) ti = ti * -1.f;
        if (link.outDirection == RoadLaneDirection::Backward) to = to * -1.f;
        const V3 a = p0 + si * (inLat * fi.sample.scaleX) + ui * 0.09f;
        const V3 d = p1 + so * (outLat * fo.sample.scaleX) + uo * 0.09f;
        // A radius-sized handle works for ordinary junctions, but can overshoot
        // badly when two short/acute arms leave their lane endpoints close
        // together. Bound it by the endpoint chord so the cubic stays local.
        const float handle = std::min(node.value().junctionRadius * 0.65f, length(d - a) * 0.5f);
        const V3    b      = a + ti * handle;
        const V3    c      = d - to * handle;

        RoadPolyline poly;
        poly.r               = 0.25f;
        poly.g               = 0.95f;
        poly.b               = 0.45f;
        poly.width           = options.navRibbonHalfWidth * 2.f;
        poly.inEdge          = link.inEdge;
        poly.outEdge         = link.outEdge;
        poly.inLane          = link.inLane;
        poly.outLane         = link.outLane;
        poly.inDirection     = link.inDirection;
        poly.outDirection    = link.outDirection;
        poly.speedLimitMps   = std::min(inEdge.value().style.speedLimitMps, outEdge.value().style.speedLimitMps);
        poly.trafficPriority = inEdge.value().style.trafficPriority;
        const int samples    = std::max(4, options.turnSamples);
        V3        prev{};
        bool      hasPrev = false;
        for (int i = 0; i <= samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            const float u = 1.f - t;
            const V3    p = a * (u * u * u) + b * (3.f * u * u * t) + c * (3.f * u * t * t) + d * (t * t * t);
            poly.xyz.push_back(p.x);
            poly.xyz.push_back(p.y);
            poly.xyz.push_back(p.z);
            if (hasPrev) {
                const V3 delta = p - prev;
                if (length(delta) > 1e-5f) {
                    const V3    fwd = normalize(delta);
                    const V3    up  = normalize(ui + uo);
                    const V3    lat = normalize(cross(up, fwd));
                    const float hw  = options.navRibbonHalfWidth * 0.9f;
                    appendStripQuad(mesh, prev + lat * -hw, p + lat * -hw, p + lat * hw, prev + lat * hw, up, 0.f, 1.f,
                                    0.f, 1.f, RoadMaterial::Nav);
                }
            }
            if (i == samples / 2 && length(d - a) > 1e-5f) {
                // Follow the curve at the arrow position. Using the endpoint chord
                // points across a hairpin and can visibly cross the turn ribbon.
                const V3 derivative = (b - a) * (3.f * u * u) + (c - b) * (6.f * u * t) + (d - c) * (3.f * t * t);
                const V3 fwd        = length(derivative) > 1e-5f ? normalize(derivative) : normalize(d - a);
                appendArrow(mesh, p, fwd, normalize(ui + uo), 0.7f);
            }
            prev    = p;
            hasPrev = true;
        }
        overlay.turns.push_back(std::move(poly));
        if (isMeshBudgetExceeded(mesh, options))
            return bakeFail<void>(DiagnosticCode::PreconditionViolation,
                                  "road navigation exceeds the mesh element budget", "mesh");
    }
    return Result<void>::success();
}


}  // namespace eve::procgen::road::detail
