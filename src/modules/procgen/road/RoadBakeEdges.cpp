#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"

#include "common/Diagnostic.h"
#include "procgen/road/RoadAuthoringInternal.h"
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

Result<void> bakeEdgeGeometry(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network, const RoadEdge& edge,
                              const RoadBakeOptions&                   options,
                              const std::unordered_set<std::uint32_t>& activeJunctionNodes,
                              const std::unordered_set<std::uint32_t>& terminalNodes, RoadFrameCache& frameCache) {
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto pathLength = spline.value().lengthResult(24);
    if (!pathLength.ok()) return Result<void>::failure(pathLength.status());

    auto fromNode = network.nodeResult(edge.from);
    auto toNode   = network.nodeResult(edge.to);
    if (!fromNode.ok() || !toNode.ok())
        return bakeFail<void>(DiagnosticCode::NotFound, "edge endpoints missing during bake");

    // A junction may not consume more than half of a short edge. Keep this
    // clamp shared with junction mouths and navigation turn anchors.
    const float trimStart = activeJunctionNodes.contains(fromNode.value().id)
                                ? junctionSocketDistance(network, fromNode.value(), edge, pathLength.value())
                                : 0.f;
    const float trimEnd   = activeJunctionNodes.contains(toNode.value().id)
                                ? junctionSocketDistance(network, toNode.value(), edge, pathLength.value())
                                : 0.f;
    if (trimStart + trimEnd >= pathLength.value() - 0.5f)
        return Result<void>::success();  // fully inside junction; skip strip

    auto frames = prepareRoadFrames(frameCache, spline.value(), edge, options, pathLength.value());
    if (!frames.ok()) return Result<void>::failure(frames.status());

    // Keep mid frames, then force exact trim endpoints so the loft meets the
    // junction apron (uniform samples alone leave ~segment-sized salmon gaps).
    std::vector<SplineFrameSample> trimmed;
    trimmed.reserve(frames.value().get().size() + 2);
    if (trimStart > 1e-3f) {
        auto tip = spline.value().travelFrameResult(trimStart, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    } else {
        trimmed.push_back(frames.value().get().front());
    }
    const float endDist = pathLength.value() - trimEnd;
    for (const auto& frame : frames.value().get()) {
        const float d = frame.sample.normalizedDistance * pathLength.value();
        if (d > trimStart + 1e-3f && d < endDist - 1e-3f) trimmed.push_back(frame);
    }
    if (trimEnd > 1e-3f) {
        auto tip = spline.value().travelFrameResult(endDist, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    } else {
        trimmed.push_back(frames.value().get().back());
    }
    if (trimmed.size() < 2) return Result<void>::success();

    // The edge owns the transition into the shared junction plane. The last
    // cross-section is therefore bit-for-bit compatible with the socket used
    // by bakeJunction, while the preceding rings absorb grade and crossfall.
    if (trimStart > 1e-3f && fromNode.value().connection.ports.empty()) {
        const JunctionPlane plane      = fitJunctionPlane(network, fromNode.value());
        const float         transition = std::max(3.f, fromNode.value().junctionRadius);
        for (auto& frame : trimmed) {
            const float distance = frame.sample.normalizedDistance * pathLength.value() - trimStart;
            if (distance > transition) continue;
            const float t = std::clamp(1.f - std::max(0.f, distance) / transition, 0.f, 1.f);
            blendFrameToJunctionPlane(frame, plane, t * t * (3.f - 2.f * t));
        }
    }
    if (trimEnd > 1e-3f && toNode.value().connection.ports.empty()) {
        const JunctionPlane plane      = fitJunctionPlane(network, toNode.value());
        const float         transition = std::max(3.f, toNode.value().junctionRadius);
        for (auto& frame : trimmed) {
            const float distance = endDist - frame.sample.normalizedDistance * pathLength.value();
            if (distance > transition) continue;
            const float t = std::clamp(1.f - std::max(0.f, distance) / transition, 0.f, 1.f);
            blendFrameToJunctionPlane(frame, plane, t * t * (3.f - 2.f * t));
        }
    }

    if (edge.style.sidePlacement.pathSampling == RoadSidePathSampling::SurfaceSegments) {
        // Preserve one bake-owned metric path. Only the visible interval contains emitted strip rows;
        // off-strip prefix/suffix retain author-relative exclusions and transition distances.
        RoadSurfaceRows surfaceRows;
        const float     firstParameter = trimmed.front().sample.normalizedDistance;
        const float     lastParameter  = trimmed.back().sample.normalizedDistance;
        for (const auto& frame : frames.value().get())
            if (frame.sample.normalizedDistance < firstParameter) surfaceRows.frames.push_back(frame);
        surfaceRows.firstVisible = surfaceRows.frames.size();
        surfaceRows.frames.insert(surfaceRows.frames.end(), trimmed.begin(), trimmed.end());
        surfaceRows.lastVisible = surfaceRows.frames.size() - 1;
        for (const auto& frame : frames.value().get())
            if (frame.sample.normalizedDistance > lastParameter) surfaceRows.frames.push_back(frame);
        frameCache.surfaces.emplace(edge.id, std::move(surfaceRows));
    }

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    auto lofted = bakeStructuralGeometry(mesh, trimmed, edge, profile.value(), pathLength.value(), options);
    if (!lofted.ok()) return lofted;
    // Cap stub ends fully. On large junction trims, only close curb/sidewalk
    // shoulders — a full U-cap draws a dark asphalt tip bar that reads as a gap.
    if (trimStart <= 0.05f && terminalNodes.contains(fromNode.value().id) &&
        fromNode.value().connection.ports.empty()) {
        {
            auto capped = capProfileRing(mesh, trimmed.front(), profile.value(), false);
            if (!capped.ok()) return capped;
        }
    } else if (trimStart > 0.05f && fromNode.value().connection.ports.empty()) {
        if (trimStart < 2.5f) {
            auto capped = capProfileRing(mesh, trimmed.front(), profile.value(), false);
            if (!capped.ok()) return capped;
        } else
            capProfileShoulders(mesh, trimmed.front(), profile.value(), false);
    }
    if (trimEnd <= 0.05f && terminalNodes.contains(toNode.value().id) && toNode.value().connection.ports.empty()) {
        {
            auto capped = capProfileRing(mesh, trimmed.back(), profile.value(), true);
            if (!capped.ok()) return capped;
        }
    } else if (trimEnd > 0.05f && toNode.value().connection.ports.empty()) {
        if (trimEnd < 2.5f) {
            auto capped = capProfileRing(mesh, trimmed.back(), profile.value(), true);
            if (!capped.ok()) return capped;
        } else
            capProfileShoulders(mesh, trimmed.back(), profile.value(), true);
    }
    auto facilities =
        bakeContinuousFacilities(mesh, trimmed, network, edge, pathLength.value(), options, activeJunctionNodes);
    if (!facilities.ok()) return facilities;
    if (options.includeMarkings) {
        auto marks = addLaneMarkings(mesh, trimmed, edge, pathLength.value(), options);
        if (!marks.ok()) return marks;
    }

    if (options.includeNavigation) {
        for (int lane = 0; lane < edge.lanesForward; ++lane) {
            const float lateral =
                laneCenterOffset(edge.style, edge.lanesForward, edge.lanesBackward, lane, RoadLaneDirection::Forward);
            RoadPolyline poly;
            poly.r               = 0.15f;
            poly.g               = 0.9f;
            poly.b               = 1.f;
            poly.width           = options.navRibbonHalfWidth * 2.f;
            poly.inEdge          = edge.id;
            poly.inLane          = lane;
            poly.inDirection     = RoadLaneDirection::Forward;
            poly.outDirection    = RoadLaneDirection::Forward;
            poly.speedLimitMps   = edge.style.speedLimitMps;
            poly.trafficPriority = edge.style.trafficPriority;
            float traveled       = 0.f;
            float nextArrow      = options.arrowSpacing * 0.5f;
            V3    prev{};
            bool  hasPrev = false;
            for (const auto& frame : trimmed) {
                const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
                const V3 side{frame.sideX, frame.sideY, frame.sideZ};
                const V3 up{frame.upX, frame.upY, frame.upZ};
                if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;
                const V3 pos = origin + side * (lateral * frame.sample.scaleX) +
                               up * (roadSurfaceHeight(edge.style, lateral) + 0.08f);
                poly.xyz.push_back(pos.x);
                poly.xyz.push_back(pos.y);
                poly.xyz.push_back(pos.z);
                if (hasPrev) {
                    const float step = length(pos - prev);
                    if (std::isfinite(step) && step > 1e-5f && step < 1.0e4f) {
                        traveled += step;
                        const V3    fwd = normalize(pos - prev);
                        const V3    lat = normalize(cross(up, fwd));
                        const float hw  = options.navRibbonHalfWidth;
                        appendStripQuad(mesh, prev + lat * -hw, pos + lat * -hw, pos + lat * hw, prev + lat * hw, up,
                                        0.f, 1.f, 0.f, 1.f, RoadMaterial::Nav);
                        int arrowGuard = 0;
                        while (traveled >= nextArrow && arrowGuard++ < 64) {
                            appendArrow(mesh, pos, fwd, up, 0.85f);
                            nextArrow += std::max(options.arrowSpacing, 0.5f);
                        }
                        if (arrowGuard >= 64) nextArrow = traveled + std::max(options.arrowSpacing, 0.5f);
                    }
                }
                prev    = pos;
                hasPrev = true;
            }
            overlay.lanes.push_back(std::move(poly));
        }
        for (int lane = 0; lane < edge.lanesBackward; ++lane) {
            const float lateral =
                laneCenterOffset(edge.style, edge.lanesForward, edge.lanesBackward, lane, RoadLaneDirection::Backward);
            RoadPolyline poly;
            poly.r               = 0.2f;
            poly.g               = 0.75f;
            poly.b               = 1.f;
            poly.width           = options.navRibbonHalfWidth * 2.f;
            poly.inEdge          = edge.id;
            poly.inLane          = lane;
            poly.inDirection     = RoadLaneDirection::Backward;
            poly.outDirection    = RoadLaneDirection::Backward;
            poly.speedLimitMps   = edge.style.speedLimitMps;
            poly.trafficPriority = edge.style.trafficPriority;
            for (auto it = trimmed.rbegin(); it != trimmed.rend(); ++it) {
                const V3 origin{it->sample.x, it->sample.y, it->sample.z};
                const V3 side{it->sideX, it->sideY, it->sideZ};
                const V3 up{it->upX, it->upY, it->upZ};
                const V3 pos = origin + side * (lateral * it->sample.scaleX) +
                               up * (roadSurfaceHeight(edge.style, lateral) + 0.08f);
                poly.xyz.insert(poly.xyz.end(), {pos.x, pos.y, pos.z});
            }
            overlay.lanes.push_back(std::move(poly));
        }
    }
    return Result<void>::success();
}


}  // namespace eve::procgen::road::detail
