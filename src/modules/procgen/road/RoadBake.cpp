#include "procgen/road/RoadBake.h"

#include "common/Diagnostic.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace eve::procgen::road {
namespace {

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalize(V3 a) {
    const float l = length(a);
    return l > 1e-8f ? a * (1.f / l) : V3{0.f, 1.f, 0.f};
}

template <class T>
Result<T> bakeFail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.road"));
}

Result<SplinePath> edgeToSpline(const RoadEdge& edge) {
    SplinePath path;
    auto kind = path.setKindResult("catmullRom");
    if (!kind.ok()) return Result<SplinePath>::failure(kind.status());
    path.setClosed(false);
    for (const auto& p : edge.controlPoints) {
        SplinePoint sp;
        sp.x = p.x;
        sp.y = p.y;
        sp.z = p.z;
        auto added = path.addPointResult(sp);
        if (!added.ok()) return Result<SplinePath>::failure(added.status());
    }
    return Result<SplinePath>::success(std::move(path));
}

float laneCenterOffset(const RoadStyle& style, int lanesForward, int laneIndex) {
    const float asphaltHalf = 0.5f * style.laneWidth * static_cast<float>(lanesForward);
    return -asphaltHalf + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
}

void appendBox(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float hx, float hy, float hz,
               RoadMaterial material) {
    mesh.setActiveGroup(roadMaterialGroup(material));
    const V3 corners[8] = {
        center + side * -hx + up * -hy + forward * -hz, center + side * hx + up * -hy + forward * -hz,
        center + side * hx + up * hy + forward * -hz,   center + side * -hx + up * hy + forward * -hz,
        center + side * -hx + up * -hy + forward * hz,  center + side * hx + up * -hy + forward * hz,
        center + side * hx + up * hy + forward * hz,    center + side * -hx + up * hy + forward * hz,
    };
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2}};
    const V3 normals[6]   = {forward * -1.f, forward, up * -1.f, up, side * -1.f, side};
    for (int f = 0; f < 6; ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        for (int c = 0; c < 4; ++c) {
            const V3& p = corners[faces[f][c]];
            mesh.addVertex(p.x, p.y, p.z, normals[f].x, normals[f].y, normals[f].z, static_cast<float>(c & 1),
                           static_cast<float>((c >> 1) & 1));
        }
        mesh.addTriangle(base, base + 1, base + 2);
        mesh.addTriangle(base, base + 2, base + 3);
    }
}

void appendStripQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                     RoadMaterial material) {
    mesh.setActiveGroup(roadMaterialGroup(material));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, u0, v0);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, u1, v0);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, u1, v1);
    mesh.addVertex(d.x, d.y, d.z, normal.x, normal.y, normal.z, u0, v1);
    mesh.addTriangle(base, base + 1, base + 2);
    mesh.addTriangle(base, base + 2, base + 3);
}

Result<void> loftProfileClean(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames,
                              const RoadProfile& profile, float uvMeters) {
    if (frames.size() < 2 || profile.points.size() < 2)
        return bakeFail<void>(DiagnosticCode::InvalidArgument, "loft needs >=2 frames and profile points");

    const int ringCount    = static_cast<int>(frames.size());
    const int profileCount = static_cast<int>(profile.points.size());
    std::vector<V3> ringPositions(static_cast<std::size_t>(ringCount * profileCount));

    for (int ring = 0; ring < ringCount; ++ring) {
        const auto& frame = frames[static_cast<std::size_t>(ring)];
        const V3    origin{frame.sample.x, frame.sample.y, frame.sample.z};
        const V3    side{frame.sideX, frame.sideY, frame.sideZ};
        const V3    up{frame.upX, frame.upY, frame.upZ};
        for (int i = 0; i < profileCount; ++i) {
            const auto& pp = profile.points[static_cast<std::size_t>(i)];
            ringPositions[static_cast<std::size_t>(ring * profileCount + i)] = origin + side * pp.side + up * pp.up;
        }
    }

    for (int ring = 0; ring < ringCount - 1; ++ring) {
        const float u0 = static_cast<float>(ring) / static_cast<float>(ringCount - 1) *
                         (uvMeters > 0.f ? uvMeters : 1.f);
        const float u1 = static_cast<float>(ring + 1) / static_cast<float>(ringCount - 1) *
                         (uvMeters > 0.f ? uvMeters : 1.f);
        for (int i = 0; i < profileCount - 1; ++i) {
            const auto& mat = profile.points[static_cast<std::size_t>(i)].material;
            const V3&   p00 = ringPositions[static_cast<std::size_t>(ring * profileCount + i)];
            const V3&   p01 = ringPositions[static_cast<std::size_t>(ring * profileCount + i + 1)];
            const V3&   p10 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i)];
            const V3&   p11 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i + 1)];
            const V3    normal = normalize(cross(p10 - p00, p01 - p00));
            const float v0 =
                profile.halfWidth > 1e-5f
                    ? (profile.points[static_cast<std::size_t>(i)].side / profile.halfWidth) * 0.5f + 0.5f
                    : 0.f;
            const float v1 =
                profile.halfWidth > 1e-5f
                    ? (profile.points[static_cast<std::size_t>(i + 1)].side / profile.halfWidth) * 0.5f + 0.5f
                    : 1.f;
            appendStripQuad(mesh, p00, p10, p11, p01, normal, u0, u1, v0, v1, mat);
        }
    }
    return Result<void>::success();
}

Result<void> addDeckAndPiers(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadStyle& style,
                             float halfWidth, bool includePiers) {
    if (frames.size() < 2) return Result<void>::success();
    // Deck underside strip.
    for (int ring = 0; ring < static_cast<int>(frames.size()) - 1; ++ring) {
        const auto& f0 = frames[static_cast<std::size_t>(ring)];
        const auto& f1 = frames[static_cast<std::size_t>(ring + 1)];
        const V3    o0{f0.sample.x, f0.sample.y, f0.sample.z};
        const V3    o1{f1.sample.x, f1.sample.y, f1.sample.z};
        const V3    s0{f0.sideX, f0.sideY, f0.sideZ};
        const V3    s1{f1.sideX, f1.sideY, f1.sideZ};
        const V3    u0{f0.upX, f0.upY, f0.upZ};
        const V3    u1{f1.upX, f1.upY, f1.upZ};
        const V3    a = o0 + s0 * -halfWidth + u0 * -style.deckThickness;
        const V3    b = o0 + s0 * halfWidth + u0 * -style.deckThickness;
        const V3    c = o1 + s1 * halfWidth + u1 * -style.deckThickness;
        const V3    d = o1 + s1 * -halfWidth + u1 * -style.deckThickness;
        appendStripQuad(mesh, a, b, c, d, normalize((u0 + u1) * -0.5f), 0.f, 1.f, 0.f, 1.f, RoadMaterial::Deck);
    }

    if (!includePiers || style.pierSpacing <= 1e-3f) return Result<void>::success();

    float traveled = 0.f;
    float nextPier = style.pierSpacing * 0.5f;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const auto& a = frames[i - 1];
        const auto& b = frames[i];
        const V3    pa{a.sample.x, a.sample.y, a.sample.z};
        const V3    pb{b.sample.x, b.sample.y, b.sample.z};
        const float seg = length(pb - pa);
        if (seg <= 1e-5f) continue;
        while (nextPier <= traveled + seg + 1e-4f) {
            const float t = std::clamp((nextPier - traveled) / seg, 0.f, 1.f);
            const V3    pos = pa + (pb - pa) * t;
            if (pos.y > style.pierClearance) {
                const V3 side = normalize(V3{a.sideX, a.sideY, a.sideZ} + V3{b.sideX, b.sideY, b.sideZ});
                const V3 fwd  = normalize(pb - pa);
                const V3 up{0.f, 1.f, 0.f};
                const float pierH = pos.y - style.deckThickness;
                if (pierH > 0.2f) {
                    const V3 center{pos.x, pierH * 0.5f, pos.z};
                    appendBox(mesh, center, side, up, fwd, style.pierWidth * 0.5f, pierH * 0.5f,
                              style.pierDepth * 0.5f, RoadMaterial::Pier);
                }
            }
            nextPier += style.pierSpacing;
            if (nextPier > 1.0e7f) break;
        }
        traveled += seg;
    }
    return Result<void>::success();
}

Result<void> addLaneMarkings(MeshBuild& mesh, const std::vector<SplineFrameSample>& frames, const RoadEdge& edge) {
    if (frames.size() < 2) return Result<void>::success();
    const auto& style = edge.style;
    const float asphaltHalf =
        0.5f * style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
    auto paintLine = [&](float lateral, bool dashed) {
        float traveled = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const auto& a = frames[i - 1];
            const auto& b = frames[i];
            const V3    pa{a.sample.x, a.sample.y, a.sample.z};
            const V3    pb{b.sample.x, b.sample.y, b.sample.z};
            const V3    sa{a.sideX, a.sideY, a.sideZ};
            const V3    sb{b.sideX, b.sideY, b.sideZ};
            const V3    ua{a.upX, a.upY, a.upZ};
            const V3    ub{b.upX, b.upY, b.upZ};
            const float seg = length(pb - pa);
            const bool  draw =
                !dashed || (std::fmod(traveled, style.dashLength + style.dashGap) < style.dashLength);
            if (draw && seg > 1e-5f) {
                const float hw = style.markingWidth * 0.5f;
                const V3    a0 = pa + sa * (lateral - hw) + ua * 0.02f;
                const V3    a1 = pa + sa * (lateral + hw) + ua * 0.02f;
                const V3    b1 = pb + sb * (lateral + hw) + ub * 0.02f;
                const V3    b0 = pb + sb * (lateral - hw) + ub * 0.02f;
                appendStripQuad(mesh, a0, b0, b1, a1, normalize(ua + ub), 0.f, 1.f, 0.f, 1.f, RoadMaterial::Marking);
            }
            traveled += seg;
        }
    };

    paintLine(-asphaltHalf + style.markingWidth, false);
    paintLine(asphaltHalf - style.markingWidth, false);
    for (int lane = 1; lane < edge.lanesForward; ++lane) {
        const float lateral = -asphaltHalf + style.laneWidth * static_cast<float>(lane);
        paintLine(lateral, true);
    }
    return Result<void>::success();
}

void appendArrow(MeshBuild& mesh, V3 pos, V3 forward, V3 up, float size) {
    const V3 side = normalize(cross(up, forward));
    const V3 tip  = pos + forward * size + up * 0.03f;
    const V3 left = pos - forward * size * 0.35f + side * (size * 0.45f) + up * 0.03f;
    const V3 right =
        pos - forward * size * 0.35f - side * (size * 0.45f) + up * 0.03f;
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Nav));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(tip.x, tip.y, tip.z, up.x, up.y, up.z, 0.5f, 1.f);
    mesh.addVertex(left.x, left.y, left.z, up.x, up.y, up.z, 0.f, 0.f);
    mesh.addVertex(right.x, right.y, right.z, up.x, up.y, up.z, 1.f, 0.f);
    mesh.addTriangle(base, base + 1, base + 2);
}

Result<void> bakeEdgeGeometry(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network, const RoadEdge& edge,
                              const RoadBakeOptions& options) {
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto pathLength = spline.value().lengthResult(24);
    if (!pathLength.ok()) return Result<void>::failure(pathLength.status());

    auto fromNode = network.nodeResult(edge.from);
    auto toNode   = network.nodeResult(edge.to);
    if (!fromNode.ok() || !toNode.ok())
        return bakeFail<void>(DiagnosticCode::NotFound, "edge endpoints missing during bake");

    const float trimStart = std::min(fromNode.value().junctionRadius, pathLength.value() * 0.35f);
    const float trimEnd   = std::min(toNode.value().junctionRadius, pathLength.value() * 0.35f);
    if (trimStart + trimEnd >= pathLength.value() - 0.5f)
        return Result<void>::success();  // fully inside junction; skip strip

    const int segments = std::max(4, options.pathSegmentsPerEdge);
    auto      frames   = spline.value().sampleFramesResult(segments, true, 0.f, 24);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    // Keep frames whose arc distance is inside the trimmed interval.
    std::vector<SplineFrameSample> trimmed;
    trimmed.reserve(frames.value().size());
    for (const auto& frame : frames.value()) {
        const float d = frame.sample.normalizedDistance * pathLength.value();
        if (d + 1e-3f >= trimStart && d - 1e-3f <= pathLength.value() - trimEnd) trimmed.push_back(frame);
    }
    if (trimmed.size() < 2) return Result<void>::success();

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    auto lofted = loftProfileClean(mesh, trimmed, profile.value(), edge.style.uvMeters);
    if (!lofted.ok()) return lofted;
    auto deck = addDeckAndPiers(mesh, trimmed, edge.style, profile.value().halfWidth, options.includePiers);
    if (!deck.ok()) return deck;
    if (options.includeMarkings) {
        auto marks = addLaneMarkings(mesh, trimmed, edge);
        if (!marks.ok()) return marks;
    }

    if (options.includeNavigation) {
        for (int lane = 0; lane < edge.lanesForward; ++lane) {
            const float lateral = laneCenterOffset(edge.style, edge.lanesForward, lane);
            RoadPolyline poly;
            poly.r = 0.15f;
            poly.g = 0.9f;
            poly.b = 1.f;
            poly.width = options.navRibbonHalfWidth * 2.f;
            float traveled = 0.f;
            float nextArrow = options.arrowSpacing * 0.5f;
            V3    prev{};
            bool  hasPrev = false;
            for (const auto& frame : trimmed) {
                const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
                const V3 side{frame.sideX, frame.sideY, frame.sideZ};
                const V3 up{frame.upX, frame.upY, frame.upZ};
                if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;
                const V3 pos = origin + side * lateral + up * 0.05f;
                poly.xyz.push_back(pos.x);
                poly.xyz.push_back(pos.y);
                poly.xyz.push_back(pos.z);
                if (hasPrev) {
                    const float step = length(pos - prev);
                    if (std::isfinite(step) && step > 1e-5f && step < 1.0e4f) {
                        traveled += step;
                        const V3 fwd = normalize(pos - prev);
                        const V3 lat = normalize(cross(up, fwd));
                        const float hw = options.navRibbonHalfWidth;
                        appendStripQuad(mesh, prev + lat * -hw, pos + lat * -hw, pos + lat * hw, prev + lat * hw, up,
                                        0.f, 1.f, 0.f, 1.f, RoadMaterial::Nav);
                        int arrowGuard = 0;
                        while (traveled >= nextArrow && arrowGuard++ < 64) {
                            appendArrow(mesh, pos, fwd, up, 0.55f);
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
    }
    return Result<void>::success();
}

Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node) {
    std::vector<V3> rim;
    float           y = node.y;
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id && edge.from != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok()) continue;
        const float d = edge.to == node.id ? std::max(0.f, len.value() - node.junctionRadius)
                                           : std::min(node.junctionRadius, len.value());
        auto frame = spline.value().travelFrameResult(d, "clamp", 16);
        if (!frame.ok()) continue;
        const auto& f = frame.value();
        const V3    origin{f.sample.x, f.sample.y, f.sample.z};
        const V3    side{f.sideX, f.sideY, f.sideZ};
        auto        profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) continue;
        const float hw = profile.value().halfWidth;
        rim.push_back(origin + side * -hw);
        rim.push_back(origin + side * hw);
        y = std::max(y, f.sample.y);
    }
    if (rim.size() < 3) {
        // Degenerate: disc around node.
        const int segs = 16;
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Asphalt));
        mesh.addVertex(node.x, y, node.z, 0.f, 1.f, 0.f, 0.5f, 0.5f);
        for (int i = 0; i < segs; ++i) {
            const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
            mesh.addVertex(node.x + std::cos(a) * node.junctionRadius, y,
                           node.z + std::sin(a) * node.junctionRadius, 0.f, 1.f, 0.f,
                           std::cos(a) * 0.5f + 0.5f, std::sin(a) * 0.5f + 0.5f);
        }
        for (int i = 0; i < segs; ++i) {
            const auto i0 = base + 1u + static_cast<std::uint32_t>(i);
            const auto i1 = base + 1u + static_cast<std::uint32_t>((i + 1) % segs);
            mesh.addTriangle(base, i0, i1);
        }
        return Result<void>::success();
    }

    // Fan from node center using rim points (unsorted but visually dense enough for MVP).
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Asphalt));
    const auto center = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(node.x, y, node.z, 0.f, 1.f, 0.f, 0.5f, 0.5f);
    for (const auto& p : rim) {
        mesh.addVertex(p.x, y, p.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    }
    for (std::uint32_t i = 0; i + 1 < static_cast<std::uint32_t>(rim.size()); i += 2) {
        mesh.addTriangle(center, center + 1 + i, center + 2 + i);
    }

    // Zebra strips toward first incoming edge.
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok() || len.value() < node.junctionRadius + 1.f) continue;
        auto frame = spline.value().travelFrameResult(len.value() - node.junctionRadius * 0.55f, "clamp", 16);
        if (!frame.ok()) continue;
        const auto& f = frame.value();
        const V3    origin{f.sample.x, f.sample.y, f.sample.z};
        const V3    side{f.sideX, f.sideY, f.sideZ};
        const V3    fwd{f.forwardX, f.forwardY, f.forwardZ};
        const V3    up{f.upX, f.upY, f.upZ};
        const float stripeW = edge.style.laneWidth * static_cast<float>(edge.lanesForward) * 0.5f;
        for (int s = 0; s < 5; ++s) {
            const float along = static_cast<float>(s) * 0.55f;
            const V3    c     = origin - fwd * along + up * 0.03f;
            const V3    a     = c + side * -stripeW;
            const V3    b     = c + side * stripeW;
            const V3    d     = a - fwd * 0.28f;
            const V3    e     = b - fwd * 0.28f;
            appendStripQuad(mesh, a, b, e, d, up, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Marking);
        }
        break;
    }
    return Result<void>::success();
}

Result<void> bakeTurnOverlays(MeshBuild& mesh, RoadOverlay& overlay, const RoadNetwork& network,
                              const RoadBakeOptions& options) {
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
        auto node = network.nodeResult(inEdge.value().to);
        if (!node.ok()) continue;

        auto& inPath  = paths[link.inEdge];
        auto& outPath = paths[link.outEdge];
        auto  inLen   = inPath.lengthResult(16);
        auto  outLen  = outPath.lengthResult(16);
        if (!inLen.ok() || !outLen.ok()) continue;

        const float inDist  = std::max(0.f, inLen.value() - node.value().junctionRadius);
        const float outDist = std::min(node.value().junctionRadius, outLen.value());
        auto        inFrame = inPath.travelFrameResult(inDist, "clamp", 16);
        auto        outFrame = outPath.travelFrameResult(outDist, "clamp", 16);
        if (!inFrame.ok() || !outFrame.ok()) continue;

        const float inLat  = laneCenterOffset(inEdge.value().style, inEdge.value().lanesForward, link.inLane);
        const float outLat = laneCenterOffset(outEdge.value().style, outEdge.value().lanesForward, link.outLane);

        const auto& fi = inFrame.value();
        const auto& fo = outFrame.value();
        const V3    p0{fi.sample.x, fi.sample.y, fi.sample.z};
        const V3    p1{fo.sample.x, fo.sample.y, fo.sample.z};
        const V3    si{fi.sideX, fi.sideY, fi.sideZ};
        const V3    so{fo.sideX, fo.sideY, fo.sideZ};
        const V3    ui{fi.upX, fi.upY, fi.upZ};
        const V3    uo{fo.upX, fo.upY, fo.upZ};
        const V3    ti{fi.forwardX, fi.forwardY, fi.forwardZ};
        const V3    to{fo.forwardX, fo.forwardY, fo.forwardZ};
        const V3    a = p0 + si * inLat + ui * 0.06f;
        const V3    d = p1 + so * outLat + uo * 0.06f;
        const float handle = std::max(2.f, node.value().junctionRadius * 0.65f);
        const V3    b = a + ti * handle;
        const V3    c = d - to * handle;

        RoadPolyline poly;
        poly.r = 0.2f;
        poly.g = 0.55f;
        poly.b = 1.f;
        poly.width = options.navRibbonHalfWidth * 2.f;
        const int samples = std::max(4, options.turnSamples);
        V3        prev{};
        bool      hasPrev = false;
        for (int i = 0; i <= samples; ++i) {
            const float t  = static_cast<float>(i) / static_cast<float>(samples);
            const float u  = 1.f - t;
            const V3    p  = a * (u * u * u) + b * (3.f * u * u * t) + c * (3.f * u * t * t) + d * (t * t * t);
            poly.xyz.push_back(p.x);
            poly.xyz.push_back(p.y);
            poly.xyz.push_back(p.z);
            if (hasPrev) {
                const V3 fwd = normalize(p - prev);
                const V3 up  = normalize(ui + uo);
                const V3 lat = normalize(cross(up, fwd));
                const float hw = options.navRibbonHalfWidth * 0.85f;
                appendStripQuad(mesh, prev + lat * -hw, p + lat * -hw, p + lat * hw, prev + lat * hw, up, 0.f, 1.f, 0.f,
                                1.f, RoadMaterial::Nav);
            }
            if (i == samples / 2) {
                const V3 fwd = normalize(d - a);
                appendArrow(mesh, p, fwd, normalize(ui + uo), 0.45f);
            }
            prev    = p;
            hasPrev = true;
        }
        overlay.turns.push_back(std::move(poly));
    }
    return Result<void>::success();
}

}  // namespace

Result<RoadBakeResult> bakeRoadNetwork(const RoadNetwork& network, const RoadBakeOptions& options) {
    if (options.pathSegmentsPerEdge < 2 || options.turnSamples < 2)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvalidArgument, "segment counts must be >= 2");
    if (network.edgeCount() == 0)
        return bakeFail<RoadBakeResult>(DiagnosticCode::PreconditionViolation, "road network has no edges");

    RoadBakeResult result;
    for (const auto& edge : network.edges()) {
        auto baked = bakeEdgeGeometry(result.mesh, result.overlay, network, edge, options);
        if (!baked.ok()) return Result<RoadBakeResult>::failure(baked.status());
    }
    if (options.includeJunctions) {
        for (const auto& node : network.nodes()) {
            auto junction = bakeJunction(result.mesh, network, node);
            if (!junction.ok()) return Result<RoadBakeResult>::failure(junction.status());
        }
    }
    if (options.includeNavigation) {
        auto turns = bakeTurnOverlays(result.mesh, result.overlay, network, options);
        if (!turns.ok()) return Result<RoadBakeResult>::failure(turns.status());
    }

    result.mesh.setMeta("generator", "procgen.road");
    result.mesh.setMeta("schema", "eve.procgen.roadNetwork");
    result.mesh.setMeta("schemaVersion", "1");
    result.mesh.setMeta("edges", std::to_string(network.edgeCount()));
    result.mesh.setMeta("nodes", std::to_string(network.nodeCount()));
    result.mesh.setMeta("laneLinks", std::to_string(network.laneLinkCount()));
    if (result.mesh.getVertexCount() < 3)
        return bakeFail<RoadBakeResult>(DiagnosticCode::InvariantViolation, "bake produced an empty mesh");
    return Result<RoadBakeResult>::success(std::move(result));
}

}  // namespace eve::procgen::road
