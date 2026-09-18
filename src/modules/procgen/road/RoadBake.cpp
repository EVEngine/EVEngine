#include "procgen/road/RoadBake.h"

#include "common/Diagnostic.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

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

/** @brief Emit a quad whose winding matches @p normal (mesh3D expects object-space CCW). */
void appendOrientedQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, float u0, float u1, float v0, float v1,
                        RoadMaterial material) {
    const V3 n = normalize(normal);
    // Tris (a,b,c) / (a,c,d); flip corner order when the geometric normal fights n.
    if (dot(cross(b - a, c - a), n) < 0.f) {
        appendStripQuad(mesh, a, d, c, b, n, u0, u1, v1, v0, material);
    } else {
        appendStripQuad(mesh, a, b, c, d, n, u0, u1, v0, v1, material);
    }
}

void appendOrientedTri(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 normal, RoadMaterial material) {
    const V3 n = normalize(normal);
    mesh.setActiveGroup(roadMaterialGroup(material));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, n.x, n.y, n.z, 0.5f, 0.5f);
    mesh.addVertex(b.x, b.y, b.z, n.x, n.y, n.z, 0.5f, 0.5f);
    mesh.addVertex(c.x, c.y, c.z, n.x, n.y, n.z, 0.5f, 0.5f);
    if (dot(cross(b - a, c - a), n) < 0.f) mesh.addTriangle(base, base + 2, base + 1);
    else
        mesh.addTriangle(base, base + 1, base + 2);
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
            const auto& a = profile.points[static_cast<std::size_t>(i)];
            const auto& b = profile.points[static_cast<std::size_t>(i + 1)];
            // Prefer asphalt for near-horizontal driving strips (endpoint material alone
            // would tag the asphalt span as curb because the left corner is a curb drop).
            RoadMaterial mat = a.material;
            if (std::fabs(a.up - b.up) <= 1e-3f &&
                (a.material == RoadMaterial::Asphalt || b.material == RoadMaterial::Asphalt)) {
                mat = RoadMaterial::Asphalt;
            }
            const V3&   p00 = ringPositions[static_cast<std::size_t>(ring * profileCount + i)];
            const V3&   p01 = ringPositions[static_cast<std::size_t>(ring * profileCount + i + 1)];
            const V3&   p10 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i)];
            const V3&   p11 = ringPositions[static_cast<std::size_t>((ring + 1) * profileCount + i + 1)];
            const V3    normal = normalize(cross(p10 - p00, p01 - p00));
            const float v0 =
                profile.halfWidth > 1e-5f
                    ? (a.side / profile.halfWidth) * 0.5f + 0.5f
                    : 0.f;
            const float v1 =
                profile.halfWidth > 1e-5f
                    ? (b.side / profile.halfWidth) * 0.5f + 0.5f
                    : 1.f;
            appendStripQuad(mesh, p00, p10, p11, p01, normal, u0, u1, v0, v1, mat);
        }
    }
    return Result<void>::success();
}

/** @brief Cap a lofted profile ring so junction-facing open U-channels are not see-through. */
void capProfileRing(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward) {
    if (profile.points.size() < 3) return;
    const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
    const V3 side{frame.sideX, frame.sideY, frame.sideZ};
    const V3 up{frame.upX, frame.upY, frame.upZ};
    V3       fwd{frame.forwardX, frame.forwardY, frame.forwardZ};
    if (!outward) fwd = fwd * -1.f;
    const V3 nrm = normalize(fwd);
    std::vector<V3> ring;
    ring.reserve(profile.points.size());
    for (const auto& pp : profile.points) ring.push_back(origin + side * pp.side + up * pp.up);
    // Fan-fill the end polygon (closes the jersey-barrier U looking into the hub).
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Curb));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    for (const V3& p : ring) mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
    for (std::size_t i = 1; i + 1 < ring.size(); ++i) {
        if (outward) mesh.addTriangle(base, base + static_cast<std::uint32_t>(i), base + static_cast<std::uint32_t>(i + 1));
        else
            mesh.addTriangle(base, base + static_cast<std::uint32_t>(i + 1), base + static_cast<std::uint32_t>(i));
    }
}

/**
 * @brief Close only curb/sidewalk shoulders at a tip — leave the asphalt U open.
 *
 * Full `capProfileRing` draws a dark tip bar across the asphalt that reads as a
 * salmon seam from above on large junction trims. Shoulder-only caps still stop
 * see-through holes at the outer gray edge.
 */
void capProfileShoulders(MeshBuild& mesh, const SplineFrameSample& frame, const RoadProfile& profile, bool outward) {
    if (profile.points.size() < 6) return;
    const V3 origin{frame.sample.x, frame.sample.y, frame.sample.z};
    const V3 side{frame.sideX, frame.sideY, frame.sideZ};
    const V3 up{frame.upX, frame.upY, frame.upZ};
    V3       fwd{frame.forwardX, frame.forwardY, frame.forwardZ};
    if (!outward) fwd = fwd * -1.f;
    const V3 nrm = normalize(fwd);

    // First Asphalt point marks the deck span start; shoulders are everything else.
    std::size_t asphaltBegin = profile.points.size();
    for (std::size_t i = 0; i < profile.points.size(); ++i) {
        if (profile.points[i].material == RoadMaterial::Asphalt) {
            asphaltBegin = i;
            break;
        }
    }
    if (asphaltBegin == 0 || asphaltBegin >= profile.points.size()) return;

    auto emitFan = [&](std::size_t begin, std::size_t end, RoadMaterial mat) {
        if (end < begin + 3) return;
        mesh.setActiveGroup(roadMaterialGroup(mat));
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        for (std::size_t i = begin; i < end; ++i) {
            const auto& pp = profile.points[i];
            const V3    p  = origin + side * pp.side + up * pp.up;
            mesh.addVertex(p.x, p.y, p.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        }
        const auto count = static_cast<std::uint32_t>(end - begin);
        for (std::uint32_t i = 1; i + 1 < count; ++i) {
            if (outward) mesh.addTriangle(base, base + i, base + i + 1);
            else
                mesh.addTriangle(base, base + i + 1, base + i);
        }
    };

    // Left shoulder: outer sidewalk → curb down to asphalt lip (excludes asphalt point).
    emitFan(0, asphaltBegin, RoadMaterial::Sidewalk);
    // Right shoulder: asphalt lip → curb → outer sidewalk.
    emitFan(asphaltBegin, profile.points.size(), RoadMaterial::Sidewalk);
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
                // World-up pier: never reuse Frenet side (can flip / tilt and skew the box).
                V3 fwdFlat{pb.x - pa.x, 0.f, pb.z - pa.z};
                const float fwdLen = length(fwdFlat);
                fwdFlat = fwdLen > 1e-5f ? fwdFlat * (1.f / fwdLen) : V3{1.f, 0.f, 0.f};
                const V3 up{0.f, 1.f, 0.f};
                const V3 side = normalize(cross(up, fwdFlat));
                const float pierH = pos.y - style.deckThickness;
                if (pierH > 0.2f) {
                    const V3 center{pos.x, pierH * 0.5f, pos.z};
                    appendBox(mesh, center, side, up, fwdFlat, style.pierWidth * 0.5f, pierH * 0.5f,
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
    auto paintLine = [&](float lateral, bool dashed, RoadMaterial material) {
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
                const V3    a0 = pa + sa * (lateral - hw) + ua * 0.035f;
                const V3    a1 = pa + sa * (lateral + hw) + ua * 0.035f;
                const V3    b1 = pb + sb * (lateral + hw) + ub * 0.035f;
                const V3    b0 = pb + sb * (lateral - hw) + ub * 0.035f;
                appendStripQuad(mesh, a0, b0, b1, a1, normalize(ua + ub), 0.f, 1.f, 0.f, 1.f, material);
            }
            traveled += seg;
        }
    };

    // White edge lines.
    paintLine(-asphaltHalf + style.markingWidth, false, RoadMaterial::Marking);
    paintLine(asphaltHalf - style.markingWidth, false, RoadMaterial::Marking);
    // Yellow dashed lane dividers (reference centerline look).
    for (int lane = 1; lane < edge.lanesForward; ++lane) {
        const float lateral = -asphaltHalf + style.laneWidth * static_cast<float>(lane);
        paintLine(lateral, true, RoadMaterial::MarkingYellow);
    }
    return Result<void>::success();
}

void appendArrow(MeshBuild& mesh, V3 pos, V3 forward, V3 up, float size) {
    const V3 side = normalize(cross(up, forward));
    const V3 tip  = pos + forward * size + up * 0.05f;
    const V3 left = pos - forward * size * 0.35f + side * (size * 0.5f) + up * 0.05f;
    const V3 right =
        pos - forward * size * 0.35f - side * (size * 0.5f) + up * 0.05f;
    mesh.setActiveGroup(roadMaterialGroup(RoadMaterial::Nav));
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(tip.x, tip.y, tip.z, up.x, up.y, up.z, 0.5f, 1.f);
    mesh.addVertex(left.x, left.y, left.z, up.x, up.y, up.z, 0.f, 0.f);
    mesh.addVertex(right.x, right.y, right.z, up.x, up.y, up.z, 1.f, 0.f);
    mesh.addTriangle(base, base + 1, base + 2);
    // Back face so arrows stay visible under top-down interchange cameras.
    const auto back = static_cast<std::uint32_t>(mesh.getVertexCount());
    const V3   nd   = up * -1.f;
    mesh.addVertex(tip.x, tip.y, tip.z, nd.x, nd.y, nd.z, 0.5f, 1.f);
    mesh.addVertex(right.x, right.y, right.z, nd.x, nd.y, nd.z, 1.f, 0.f);
    mesh.addVertex(left.x, left.y, left.z, nd.x, nd.y, nd.z, 0.f, 0.f);
    mesh.addTriangle(back, back + 1, back + 2);
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

    // Honour full junctionRadius so filleted corners meet the arm tips with G1
    // tangents (square stubs from early trim read as sharp turn corners).
    const float maxTrim   = std::max(0.f, pathLength.value() * 0.5f - 0.5f);
    // Honour full junctionRadius so filleted corners meet the arm tips with G1
    // tangents (square stubs from early trim read as sharp turn corners).
    const float trimStart = std::min(std::max(0.f, fromNode.value().junctionRadius), maxTrim);
    const float trimEnd   = std::min(std::max(0.f, toNode.value().junctionRadius), maxTrim);
    if (trimStart + trimEnd >= pathLength.value() - 0.5f)
        return Result<void>::success();  // fully inside junction; skip strip

    const int segments = std::max(4, options.pathSegmentsPerEdge);
    auto      frames   = spline.value().sampleFramesResult(segments, true, 0.f, 24);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    // Keep mid frames, then force exact trim endpoints so the loft meets the
    // junction apron (uniform samples alone leave ~segment-sized salmon gaps).
    std::vector<SplineFrameSample> trimmed;
    trimmed.reserve(frames.value().size() + 2);
    if (trimStart > 1e-3f) {
        auto tip = spline.value().travelFrameResult(trimStart, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    }
    const float endDist = pathLength.value() - trimEnd;
    for (const auto& frame : frames.value()) {
        const float d = frame.sample.normalizedDistance * pathLength.value();
        if (d > trimStart + 1e-3f && d < endDist - 1e-3f) trimmed.push_back(frame);
    }
    if (trimEnd > 1e-3f) {
        auto tip = spline.value().travelFrameResult(endDist, "clamp", 24);
        if (!tip.ok()) return Result<void>::failure(tip.status());
        trimmed.push_back(std::move(tip).takeValue());
    }
    if (trimmed.size() < 2) return Result<void>::success();

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    auto lofted = loftProfileClean(mesh, trimmed, profile.value(), edge.style.uvMeters);
    if (!lofted.ok()) return lofted;
    // Cap stub ends fully. On large junction trims, only close curb/sidewalk
    // shoulders — a full U-cap draws a dark asphalt tip bar that reads as a gap.
    if (trimStart > 0.05f) {
        if (trimStart < 2.5f) capProfileRing(mesh, trimmed.front(), profile.value(), false);
        else
            capProfileShoulders(mesh, trimmed.front(), profile.value(), false);
    }
    if (trimEnd > 0.05f) {
        if (trimEnd < 2.5f) capProfileRing(mesh, trimmed.back(), profile.value(), true);
        else
            capProfileShoulders(mesh, trimmed.back(), profile.value(), true);
    }
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
                const V3 pos = origin + side * lateral + up * 0.08f;
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
    }
    return Result<void>::success();
}

Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node) {
    struct ArmTip {
        V3    origin;
        float asphaltHalf = 0.f;
        float halfWidth   = 0.f;
        float curbHeight  = 0.f;
        float sidewalkH   = 0.f;
    };
    std::vector<ArmTip> arms;
    float               maxHalfWidth = 0.f;
    float               maxAsphalt   = 0.f;
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
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) continue;
        const float asphaltHalf =
            0.5f * edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
        const auto& f = frame.value();
        ArmTip      tip;
        tip.origin      = V3{f.sample.x, f.sample.y, f.sample.z};
        tip.asphaltHalf = asphaltHalf;
        tip.halfWidth   = profile.value().halfWidth;
        tip.curbHeight  = edge.style.curbHeight;
        tip.sidewalkH   = edge.style.sidewalkHeight;
        arms.push_back(tip);
        maxHalfWidth = std::max(maxHalfWidth, tip.halfWidth);
        maxAsphalt   = std::max(maxAsphalt, tip.asphaltHalf);
    }
    if (static_cast<int>(arms.size()) < 2) return Result<void>::success();

    const float y        = node.y;
    const float asphaltY = y + 0.01f;
    const float jr       = node.junctionRadius;

    // Detect an axis-aligned 4-way cross (the simple debug scene).
    bool axisCross = arms.size() == 4;
    if (axisCross) {
        int axisHits = 0;
        for (const auto& tip : arms) {
            const float dx = std::fabs(tip.origin.x - node.x);
            const float dz = std::fabs(tip.origin.z - node.z);
            if ((dx < 0.35f && dz > jr * 0.4f) || (dz < 0.35f && dx > jr * 0.4f)) ++axisHits;
        }
        axisCross = axisHits == 4;
    }

    // Asphalt fill. Fan from a hub vertex — a single large strip-quad can vanish
    // under Lavapipe even when the CPU triangles cover the origin.
    if (axisCross) {
        // Curb-return fillet: circle centers sit in the outer corner (jr,jr), so the
        // asphalt edge is concave and sidewalks wrap toward the property corner.
        const float ah      = maxAsphalt;
        const float cornerR = std::max(0.75f, jr - ah);
        const int   segs    = 16;
        const V3    upN{0.f, 1.f, 0.f};

        // Hub fan along the filleted outline (arm tips + concave corner arcs).
        {
            std::vector<std::pair<float, float>> rim;
            rim.reserve(static_cast<std::size_t>(4 * (segs + 3)));
            auto pushPt = [&](float x, float z) {
                if (!rim.empty() && std::fabs(rim.back().first - x) < 1e-4f &&
                    std::fabs(rim.back().second - z) < 1e-4f)
                    return;
                rim.emplace_back(x, z);
            };
            auto pushCornerArc = [&](int sx, int sz, bool reverse) {
                const float sxf = static_cast<float>(sx);
                const float szf = static_cast<float>(sz);
                const float cx  = node.x + sxf * jr;
                const float cz  = node.z + szf * jr;
                for (int i = 0; i <= segs; ++i) {
                    const int   ii = reverse ? (segs - i) : i;
                    const float t  = static_cast<float>(ii) / static_cast<float>(segs);
                    const float th = t * 1.5707963f;
                    pushPt(cx - sxf * cornerR * std::sin(th), cz - szf * cornerR * std::cos(th));
                }
            };
            pushPt(node.x + jr, node.z - ah);
            pushPt(node.x + jr, node.z + ah);
            pushCornerArc(+1, +1, false);
            pushPt(node.x - ah, node.z + jr);
            pushCornerArc(-1, +1, true);
            pushPt(node.x - jr, node.z - ah);
            pushCornerArc(-1, -1, false);
            pushPt(node.x + ah, node.z - jr);
            pushCornerArc(+1, -1, true);

            const V3 hub{node.x, asphaltY, node.z};
            for (std::size_t i = 0; i < rim.size(); ++i) {
                const auto& p0 = rim[i];
                const auto& p1 = rim[(i + 1) % rim.size()];
                appendOrientedTri(mesh, hub, V3{p0.first, asphaltY, p0.second},
                                  V3{p1.first, asphaltY, p1.second}, upN, RoadMaterial::Asphalt);
            }

            // Thin seal strips under each arm tip — hides residual sub-cm seams.
            const float seal  = 0.12f;
            const float ySeal = asphaltY + 0.002f;
            auto        addSeal = [&](float x0, float z0, float x1, float z1, float x2, float z2, float x3,
                               float z3) {
                appendOrientedQuad(mesh, V3{x0, ySeal, z0}, V3{x1, ySeal, z1}, V3{x2, ySeal, z2}, V3{x3, ySeal, z3},
                                   upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Asphalt);
            };
            addSeal(node.x - ah, node.z + jr - seal, node.x + ah, node.z + jr - seal, node.x + ah, node.z + jr + seal,
                    node.x - ah, node.z + jr + seal);
            addSeal(node.x - ah, node.z - jr - seal, node.x + ah, node.z - jr - seal, node.x + ah, node.z - jr + seal,
                    node.x - ah, node.z - jr + seal);
            addSeal(node.x + jr - seal, node.z - ah, node.x + jr + seal, node.z - ah, node.x + jr + seal, node.z + ah,
                    node.x + jr - seal, node.z + ah);
            addSeal(node.x - jr - seal, node.z - ah, node.x - jr + seal, node.z - ah, node.x - jr + seal, node.z + ah,
                    node.x - jr - seal, node.z + ah);
        }

        const float curbW = 0.35f;
        const float walkW = std::max(0.4f, maxHalfWidth - ah - curbW);
        float       curbH = 0.45f;
        float       walkH = 0.14f;
        if (!arms.empty()) {
            curbH = arms.front().curbHeight;
            walkH = arms.front().sidewalkH;
        }
        const float walkY   = y + walkH;
        const float curbTop = y + curbH;
        const float r0 = cornerR;
        const float r1 = std::max(0.2f, cornerR - curbW);
        const float r2 = std::max(0.15f, cornerR - curbW - walkW);
        for (int sx : {-1, 1}) {
            for (int sz : {-1, 1}) {
                const float sxf = static_cast<float>(sx);
                const float szf = static_cast<float>(sz);
                const float cx  = node.x + sxf * jr;
                const float cz  = node.z + szf * jr;
                auto        arcPt = [&](float radius, float t) {
                    const float th = t * 1.5707963f;
                    return V3{cx - sxf * radius * std::sin(th), 0.f, cz - szf * radius * std::cos(th)};
                };
                auto wall = [&](V3 p0, V3 p1, float y0, float y1, V3 n, RoadMaterial mat) {
                    appendOrientedQuad(mesh, V3{p0.x, y0, p0.z}, V3{p1.x, y0, p1.z}, V3{p1.x, y1, p1.z},
                                       V3{p0.x, y1, p0.z}, n, 0.f, 1.f, 0.f, 1.f, mat);
                    appendOrientedQuad(mesh, V3{p0.x, y0, p0.z}, V3{p1.x, y0, p1.z}, V3{p1.x, y1, p1.z},
                                       V3{p0.x, y1, p0.z}, n * -1.f, 0.f, 1.f, 0.f, 1.f, mat);
                };
                for (int i = 0; i < segs; ++i) {
                    const float t0 = static_cast<float>(i) / static_cast<float>(segs);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(segs);
                    const V3    a0 = arcPt(r0, t0);
                    const V3    a1 = arcPt(r0, t1);
                    const V3    b0 = arcPt(r1, t0);
                    const V3    b1 = arcPt(r1, t1);
                    const V3    s0 = arcPt(r2, t0);
                    const V3    s1 = arcPt(r2, t1);
                    // Sidewalk top slightly past r2 toward the property to cover the skirt seam.
                    const V3    c0 = arcPt(std::max(0.05f, r2 - 0.06f), t0);
                    const V3    c1 = arcPt(std::max(0.05f, r2 - 0.06f), t1);
                    const V3    b0i = arcPt(r1 - 0.02f, t0);
                    const V3    b1i = arcPt(r1 - 0.02f, t1);
                    const V3    b0o = arcPt(r1 + 0.02f, t0);
                    const V3    b1o = arcPt(r1 + 0.02f, t1);
                    const V3 inToCorner = normalize(V3{cx - a0.x, 0.f, cz - a0.z});
                    const V3 outToRoad  = inToCorner * -1.f;

                    appendOrientedQuad(mesh, V3{a0.x, curbTop, a0.z}, V3{b0o.x, curbTop, b0o.z},
                                       V3{b1o.x, curbTop, b1o.z}, V3{a1.x, curbTop, a1.z}, upN, 0.f, 1.f, 0.f, 1.f,
                                       RoadMaterial::Curb);
                    appendOrientedQuad(mesh, V3{b0i.x, walkY, b0i.z}, V3{c0.x, walkY, c0.z}, V3{c1.x, walkY, c1.z},
                                       V3{b1i.x, walkY, b1i.z}, upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Sidewalk);

                    wall(a0, a1, y, curbTop, outToRoad, RoadMaterial::Curb);
                    wall(b0, b1, walkY, curbTop, outToRoad, RoadMaterial::Curb);
                    wall(s0, s1, y, walkY, outToRoad, RoadMaterial::Sidewalk);
                }
            }
        }
    } else {
        const float radius =
            std::sqrt(jr * jr + maxHalfWidth * maxHalfWidth) + 0.35f;
        const int segs = 32;
        const V3  hub{node.x, asphaltY, node.z};
        const V3  upN{0.f, 1.f, 0.f};
        for (int i = 0; i < segs; ++i) {
            const float a0 = static_cast<float>(i) * 6.2831853f / static_cast<float>(segs);
            const float a1 = static_cast<float>(i + 1) * 6.2831853f / static_cast<float>(segs);
            appendOrientedTri(mesh, hub,
                              V3{node.x + std::cos(a0) * radius, asphaltY, node.z + std::sin(a0) * radius},
                              V3{node.x + std::cos(a1) * radius, asphaltY, node.z + std::sin(a1) * radius}, upN,
                              RoadMaterial::Asphalt);
        }
    }

    // Zebra stripes on every arm tip that meets this hub.
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id && edge.from != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok() || len.value() < node.junctionRadius + 1.5f) continue;
        const float alongDist =
            edge.to == node.id ? (len.value() - node.junctionRadius - 1.6f)
                               : (node.junctionRadius + 1.6f);
        auto frame = spline.value().travelFrameResult(alongDist, "clamp", 16);
        if (!frame.ok()) continue;
        const auto& f = frame.value();
        const V3    origin{f.sample.x, f.sample.y, f.sample.z};
        const V3    side{f.sideX, f.sideY, f.sideZ};
        // Stripe direction: toward the hub from this tip.
        V3          fwd{f.forwardX, f.forwardY, f.forwardZ};
        if (edge.from == node.id) fwd = fwd * -1.f;
        const V3    nrm{0.f, 1.f, 0.f};
        const float stripeW = edge.style.laneWidth * static_cast<float>(edge.lanesForward) * 0.45f;
        for (int s = 0; s < 4; ++s) {
            const float along = static_cast<float>(s) * 0.5f;
            const V3    c     = origin - fwd * along + nrm * 0.04f;
            const V3    a     = c + side * -stripeW;
            const V3    b     = c + side * stripeW;
            const V3    d     = a - fwd * 0.25f;
            const V3    e     = b - fwd * 0.25f;
            appendStripQuad(mesh, a, b, e, d, nrm, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Marking);
        }
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
        const V3    a = p0 + si * inLat + ui * 0.09f;
        const V3    d = p1 + so * outLat + uo * 0.09f;
        const float handle = std::max(2.f, node.value().junctionRadius * 0.65f);
        const V3    b = a + ti * handle;
        const V3    c = d - to * handle;

        RoadPolyline poly;
        poly.r = 0.25f;
        poly.g = 0.95f;
        poly.b = 0.45f;
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
                const float hw = options.navRibbonHalfWidth * 0.9f;
                appendStripQuad(mesh, prev + lat * -hw, p + lat * -hw, p + lat * hw, prev + lat * hw, up, 0.f, 1.f, 0.f,
                                1.f, RoadMaterial::Nav);
            }
            if (i == samples / 2) {
                const V3 fwd = normalize(d - a);
                appendArrow(mesh, p, fwd, normalize(ui + uo), 0.7f);
            }
            prev    = p;
            hasPrev = true;
        }
        overlay.turns.push_back(std::move(poly));
    }
    return Result<void>::success();
}

}  // namespace

namespace {

struct MaterialColor {
    float r, g, b, a;
};

MaterialColor colorForGroup(const std::string& name) {
    if (name == "asphalt") return {0.28f, 0.28f, 0.30f, 1.f};
    if (name == "curb") return {0.78f, 0.78f, 0.76f, 1.f};
    if (name == "sidewalk") return {0.86f, 0.86f, 0.84f, 1.f};
    if (name == "deck") return {0.48f, 0.48f, 0.46f, 1.f};
    if (name == "pier") return {0.70f, 0.68f, 0.64f, 1.f};
    if (name == "marking") return {0.96f, 0.96f, 0.94f, 1.f};
    if (name == "markingYellow") return {0.96f, 0.80f, 0.10f, 1.f};
    if (name == "nav") return {0.10f, 0.95f, 1.f, 1.f};
    return {0.55f, 0.55f, 0.55f, 1.f};
}

Result<void> paintGroupVertexColors(MeshBuild& mesh) {
    const int verts = mesh.getVertexCount();
    if (verts <= 0) return Result<void>::success();
    std::vector<float> colors(static_cast<std::size_t>(verts) * 4u, 1.f);
    const int triCount = mesh.getIndexCount() / 3;
    for (int t = 0; t < triCount; ++t) {
        const int group = mesh.getTriangleGroup(t);
        const MaterialColor c =
            group >= 0 ? colorForGroup(mesh.getGroupName(group)) : MaterialColor{0.5f, 0.5f, 0.5f, 1.f};
        for (int k = 0; k < 3; ++k) {
            const int vi = mesh.getIndex(t * 3 + k);
            if (vi < 0 || vi >= verts) continue;
            const auto base = static_cast<std::size_t>(vi) * 4u;
            colors[base + 0] = c.r;
            colors[base + 1] = c.g;
            colors[base + 2] = c.b;
            colors[base + 3] = c.a;
        }
    }
    return mesh.setVertexColors(std::move(colors));
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

    auto painted = paintGroupVertexColors(result.mesh);
    if (!painted.ok()) return Result<RoadBakeResult>::failure(painted.status());

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
