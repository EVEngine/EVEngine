#include "procgen/road/RoadBake.h"

#include "common/Diagnostic.h"
#include "procgen/road/RoadDecor.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <string>
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

/**
 * @brief Lateral offset of a lane center from the edge centerline.
 *
 * Forward-only layouts keep lanes centered on the centerline. Bidirectional
 * layouts place reverse lanes on the negative side and forward lanes on the
 * positive side so the double-yellow sits on the centerline.
 */
float laneCenterOffset(const RoadStyle& style, int lanesForward, int lanesBackward, int laneIndex, bool reverse) {
    const float asphaltHalf =
        0.5f * style.laneWidth * static_cast<float>(lanesForward + std::max(0, lanesBackward));
    if (lanesBackward <= 0) {
        return -asphaltHalf + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
    }
    if (reverse) {
        return -asphaltHalf + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
    }
    const float boundary = -asphaltHalf + style.laneWidth * static_cast<float>(lanesBackward);
    return boundary + style.laneWidth * (static_cast<float>(laneIndex) + 0.5f);
}

/** @brief Intersect two infinite XZ lines; returns false when nearly parallel. */
bool intersectXZ(V3 p0, V3 d0, V3 p1, V3 d1, V3& out) {
    const float den = d0.x * d1.z - d0.z * d1.x;
    if (std::fabs(den) < 1e-6f) return false;
    const float t = ((p1.x - p0.x) * d1.z - (p1.z - p0.z) * d1.x) / den;
    out           = {p0.x + d0.x * t, 0.f, p0.z + d0.z * t};
    return true;
}

/** @brief CCW angle from Va to Vb in the XZ plane, in (0, 2π]. */
float ccwAngleXZ(V3 a, V3 b) {
    const float cross = a.x * b.z - a.z * b.x;
    const float d     = a.x * b.x + a.z * b.z;
    float       ang   = std::atan2(cross, d);
    if (ang <= 1e-5f) ang += 6.2831853f;
    return ang;
}

/** @brief Rotate a horizontal vector about +Y (CCW in atan2(z,x)). */
V3 rotateY(V3 v, float rad) {
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

/**
 * @brief One road arm arriving at a junction, with curb-return trim distance.
 *
 * For non-orthogonal junctions the true fillet tangency is closer to the hub
 * than junctionRadius; trimDist is that along-arm distance so the loft tip
 * docks flush with the curb-return arc. When a corner cannot fillet at the
 * authored approach, outDir is bent slightly (see planJunction) so common
 * acute forks still dock without salmon gaps.
 */
struct JunctionArm {
    std::uint32_t edgeId       = 0;
    bool          tipAtTo      = true;  ///< Tip is the edge.to end.
    V3            outDir;               ///< Hub → arm dock direction (may be bent).
    V3            naturalOutDir;        ///< Authored approach before bend-to-dock.
    V3            right;                ///< Right when looking outward along outDir.
    float         asphaltHalf  = 0.f;
    float         halfWidth    = 0.f;
    float         curbWidth    = 0.f;
    float         sidewalkWidth = 0.f;
    float         curbHeight   = 0.f;
    float         sidewalkH    = 0.f;
    float         trimDist     = 0.f;  ///< Hub-distance where the arm loft ends.
    float         angle        = 0.f;
    bool          bent         = false; ///< outDir was nudged so a fillet could dock.
};

void refreshArmBasis(JunctionArm& arm) {
    arm.outDir = normalize(V3{arm.outDir.x, 0.f, arm.outDir.z});
    arm.right  = normalize(cross(V3{0.f, 1.f, 0.f}, arm.outDir));
    arm.angle  = std::atan2(arm.outDir.z, arm.outDir.x);
}

struct JunctionCorner {
    bool  filleted = false;
    V3    center;
    float radius   = 0.f;
    float startAng = 0.f;
    float sweep    = 0.f;
    V3    aTan;  ///< Fillet tangency on arm A left asphalt edge.
    V3    bTan;  ///< Fillet tangency on arm B right asphalt edge.
};

struct JunctionPlan {
    std::vector<JunctionArm>    arms;
    std::vector<JunctionCorner> corners;
    bool                        ok = false;
};

/**
 * @brief Plan polar curb-returns and per-arm trim distances for one node.
 *
 * Uses true circle-tangency on asphalt edge lines so common gaps (60°/90°/
 * 120°/135°) dock without salmon gaps. Orthogonal crosses keep
 * trimDist == junctionRadius. Wide gaps (>~140°) use a straight chord.
 * When a corner cannot fillet at the authored approach, both arms are
 * spread by a few degrees (bend-to-dock) so the tip loft curves into a
 * dockable orientation.
 */
JunctionPlan planJunction(const RoadNetwork& network, const RoadNode& node) {
    JunctionPlan plan;
    for (const auto& edge : network.edges()) {
        if (edge.to != node.id && edge.from != node.id) continue;
        auto spline = edgeToSpline(edge);
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok()) continue;
        // Sample near the authored junctionRadius to get a stable outward frame.
        const float probe = edge.to == node.id ? std::max(0.f, len.value() - node.junctionRadius)
                                               : std::min(node.junctionRadius, len.value());
        auto frame = spline.value().travelFrameResult(probe, "clamp", 16);
        if (!frame.ok()) continue;
        auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
        if (!profile.ok()) continue;
        const float asphaltHalf =
            0.5f * edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward);
        const auto& f = frame.value();
        JunctionArm arm;
        arm.edgeId  = edge.id;
        arm.tipAtTo = (edge.to == node.id);
        V3 out{f.sample.x - node.x, 0.f, f.sample.z - node.z};
        if (length(out) < 1e-4f) {
            out = V3{f.forwardX, 0.f, f.forwardZ};
            if (edge.to == node.id) out = out * -1.f;
            if (length(out) < 1e-4f) out = V3{1.f, 0.f, 0.f};
        }
        arm.outDir         = normalize(out);
        arm.naturalOutDir  = arm.outDir;
        refreshArmBasis(arm);
        arm.asphaltHalf    = asphaltHalf;
        arm.halfWidth      = profile.value().halfWidth;
        arm.curbWidth      = edge.style.curbWidth;
        arm.sidewalkWidth  = edge.style.sidewalkWidth;
        arm.curbHeight     = edge.style.curbHeight;
        arm.sidewalkH      = edge.style.sidewalkHeight;
        arm.trimDist       = node.junctionRadius;
        arm.bent           = false;
        plan.arms.push_back(arm);
    }
    if (plan.arms.size() < 2) return plan;

    std::sort(plan.arms.begin(), plan.arms.end(),
              [](const JunctionArm& a, const JunctionArm& b) { return a.angle < b.angle; });

    const float jr = node.junctionRadius;

    auto tryFillet = [&](JunctionArm& A, JunctionArm& B, JunctionCorner& corner) -> bool {
        const float ang = ccwAngleXZ(A.outDir, B.outDir);
        if (ang > 2.45f) {
            // Wide chord placeholders; aTan/bTan refreshed after all trims finalize.
            corner.filleted = false;
            corner.aTan     = {};
            corner.bTan     = {};
            return true;
        }

        const float ahA = A.asphaltHalf;
        const float ahB = B.asphaltHalf;
        float       r   = std::max(0.75f, jr - 0.5f * (ahA + ahB));
        r = std::min(r, std::max(0.55f, std::tan(0.5f * std::min(ang, 3.0f)) * jr * 0.9f));

        for (int attempt = 0; attempt < 6; ++attempt) {
            const float usedR = r * (1.f - 0.11f * static_cast<float>(attempt));
            V3          center{};
            const V3    pA = V3{node.x, 0.f, node.z} - A.right * (ahA + usedR);
            const V3    pB = V3{node.x, 0.f, node.z} + B.right * (ahB + usedR);
            if (!intersectXZ(pA, A.outDir, pB, B.outDir, center)) continue;
            const V3 toC = V3{center.x - node.x, 0.f, center.z - node.z};
            if (dot(toC, A.outDir) < -0.25f * jr && dot(toC, B.outDir) < -0.25f * jr) continue;

            const V3  aTan   = center + A.right * usedR;
            const V3  bTan   = center - B.right * usedR;
            const float alongA = dot(V3{aTan.x - node.x, 0.f, aTan.z - node.z}, A.outDir);
            const float alongB = dot(V3{bTan.x - node.x, 0.f, bTan.z - node.z}, B.outDir);
            if (alongA < 0.45f || alongB < 0.45f) continue;
            if (alongA > jr * 1.55f || alongB > jr * 1.55f) continue;

            const float latA = std::fabs(dot(V3{aTan.x - node.x, 0.f, aTan.z - node.z}, A.right));
            const float latB = std::fabs(dot(V3{bTan.x - node.x, 0.f, bTan.z - node.z}, B.right));
            if (std::fabs(latA - ahA) > 0.2f || std::fabs(latB - ahB) > 0.2f) continue;

            corner.center   = center;
            corner.radius   = usedR;
            corner.aTan     = aTan;
            corner.bTan     = bTan;
            corner.startAng = std::atan2(aTan.z - center.z, aTan.x - center.x);
            const float endAng = std::atan2(bTan.z - center.z, bTan.x - center.x);
            float       ccw    = endAng - corner.startAng;
            while (ccw <= 1e-4f) ccw += 6.2831853f;
            while (ccw > 6.2831853f) ccw -= 6.2831853f;
            const float cw = ccw - 6.2831853f;
            auto        midDist = [&](float sweep) {
                const float angM = corner.startAng + 0.5f * sweep;
                const float mx   = center.x + std::cos(angM) * corner.radius;
                const float mz   = center.z + std::sin(angM) * corner.radius;
                const float dx   = mx - node.x;
                const float dz   = mz - node.z;
                return dx * dx + dz * dz;
            };
            corner.sweep    = midDist(ccw) <= midDist(cw) ? ccw : cw;
            corner.filleted = true;
            A.trimDist      = std::min(A.trimDist, alongA);
            B.trimDist      = std::min(B.trimDist, alongB);
            return true;
        }
        return false;
    };

    auto resetTrims = [&]() {
        for (auto& arm : plan.arms) arm.trimDist = jr;
    };

    auto planAllCorners = [&]() -> bool {
        plan.corners.clear();
        plan.corners.reserve(plan.arms.size());
        resetTrims();
        for (std::size_t i = 0; i < plan.arms.size(); ++i) {
            JunctionArm&   A = plan.arms[i];
            JunctionArm&   B = plan.arms[(i + 1) % plan.arms.size()];
            JunctionCorner corner;
            if (!tryFillet(A, B, corner)) return false;
            plan.corners.push_back(corner);
        }
        // Refresh wide-angle chords from finalized shared-arm trims.
        for (std::size_t i = 0; i < plan.corners.size(); ++i) {
            if (plan.corners[i].filleted) continue;
            const JunctionArm& A    = plan.arms[i];
            const JunctionArm& B    = plan.arms[(i + 1) % plan.arms.size()];
            const V3           tipA = V3{node.x, 0.f, node.z} + A.outDir * A.trimDist;
            const V3           tipB = V3{node.x, 0.f, node.z} + B.outDir * B.trimDist;
            plan.corners[i].aTan    = tipA - A.right * A.asphaltHalf;
            plan.corners[i].bTan    = tipB + B.right * B.asphaltHalf;
        }
        return plan.corners.size() == plan.arms.size();
    };

    if (planAllCorners()) {
        plan.ok = true;
        return plan;
    }

    // Bend-to-dock: spread each acute corner's arms until a fillet fits.
    // Cap at ~18° so the loft tip curves gently instead of kinking.
    constexpr float kMaxBend = 0.31415927f;  // 18°
    constexpr float kStep    = 0.034906585f; // 2°
    const std::size_t nArms  = plan.arms.size();
    std::vector<V3>   natural(nArms);
    for (std::size_t i = 0; i < nArms; ++i) natural[i] = plan.arms[i].naturalOutDir;

    std::vector<float> naturalGap(nArms);
    for (std::size_t i = 0; i < nArms; ++i) {
        naturalGap[i] = ccwAngleXZ(natural[i], natural[(i + 1) % nArms]);
    }

    for (float bend = kStep; bend <= kMaxBend + 1e-4f; bend += kStep) {
        // Detect which corners fail at the current natural dirs, then widen only
        // those gaps so adjacent bends do not cancel to a zero net yaw.
        std::vector<bool> needsWiden(nArms, false);
        bool              anyFail = false;
        for (std::size_t i = 0; i < nArms; ++i) {
            if (naturalGap[i] > 2.45f) continue;
            JunctionArm    A = plan.arms[i];
            JunctionArm    B = plan.arms[(i + 1) % nArms];
            A.outDir         = natural[i];
            B.outDir         = natural[(i + 1) % nArms];
            refreshArmBasis(A);
            refreshArmBasis(B);
            A.trimDist = jr;
            B.trimDist = jr;
            JunctionCorner corner;
            if (!tryFillet(A, B, corner)) {
                needsWiden[i] = true;
                anyFail       = true;
            }
        }
        if (!anyFail) break;

        std::vector<float> yaw(nArms, 0.f);
        for (std::size_t i = 0; i < nArms; ++i) {
            if (!needsWiden[i]) continue;
            // Only spread when the neighbouring gaps have room (wide or already ok).
            const std::size_t prev = (i + nArms - 1) % nArms;
            const std::size_t next = (i + 1) % nArms;
            if (!needsWiden[prev] || naturalGap[prev] > 2.0f) yaw[i] -= bend;
            if (!needsWiden[next] || naturalGap[next] > 2.0f) yaw[next] += bend;
        }
        float totalYaw = 0.f;
        for (float y : yaw) totalYaw += std::fabs(y);
        if (totalYaw < 1e-5f) break;

        for (std::size_t i = 0; i < nArms; ++i) {
            plan.arms[i].outDir = rotateY(natural[i], yaw[i]);
            plan.arms[i].bent   = std::fabs(yaw[i]) > 1e-5f;
            refreshArmBasis(plan.arms[i]);
        }
        if (planAllCorners()) {
            plan.ok = true;
            return plan;
        }
    }

    // Restore natural dirs for disc fallback consumers.
    for (std::size_t i = 0; i < nArms; ++i) {
        plan.arms[i].outDir = natural[i];
        plan.arms[i].bent   = false;
        refreshArmBasis(plan.arms[i]);
        plan.arms[i].trimDist = jr;
    }
    plan.corners.clear();
    plan.ok = false;
    return plan;
}

/** @brief Look up trim for one edge end from a planned junction (or fall back). */
float trimFromPlan(const JunctionPlan& plan, const RoadNode& node, std::uint32_t edgeId) {
    if (!plan.ok) return node.junctionRadius;
    for (const auto& arm : plan.arms) {
        if (arm.edgeId == edgeId) return arm.trimDist;
    }
    return node.junctionRadius;
}

/** @brief Hub-distance trim for one edge end at a junction node (falls back to jr). */
float junctionTrimDistance(const RoadNetwork& network, const RoadNode& node, std::uint32_t edgeId) {
    if (static_cast<int>(network.edges().size()) == 0) return node.junctionRadius;
    // Degree-1 nodes keep the authored radius (no fillet docking).
    int degree = 0;
    for (const auto& e : network.edges()) {
        if (e.from == node.id || e.to == node.id) ++degree;
    }
    if (degree < 2) return node.junctionRadius;
    return trimFromPlan(planJunction(network, node), node, edgeId);
}

/** @brief Safe bend window length shared by mid-frame culling and approach rings. */
float bendLength(float armTrim, float effectiveTrim) {
    return std::clamp(armTrim * 0.9f, 2.f, std::max(2.f, effectiveTrim));
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
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "loft needs >=2 frames and profile points", {}, {}, "procgen.road"));

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

    if (edge.lanesBackward > 0) {
        // Solid double-yellow on the centerline between opposing traffic.
        paintLine(-style.markingWidth * 0.65f, false, RoadMaterial::MarkingYellow);
        paintLine(style.markingWidth * 0.65f, false, RoadMaterial::MarkingYellow);
        for (int lane = 1; lane < edge.lanesBackward; ++lane) {
            const float lateral = -asphaltHalf + style.laneWidth * static_cast<float>(lane);
            paintLine(lateral, true, RoadMaterial::Marking);
        }
        for (int lane = 1; lane < edge.lanesForward; ++lane) {
            const float lateral = style.laneWidth * static_cast<float>(lane);
            paintLine(lateral, true, RoadMaterial::Marking);
        }
    } else {
        // Yellow dashed lane dividers (reference centerline look).
        for (int lane = 1; lane < edge.lanesForward; ++lane) {
            const float lateral = -asphaltHalf + style.laneWidth * static_cast<float>(lane);
            paintLine(lateral, true, RoadMaterial::MarkingYellow);
        }
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
                              const RoadBakeOptions& options,
                              const std::unordered_map<std::uint32_t, JunctionPlan>* planCache = nullptr) {
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto pathLength = spline.value().lengthResult(24);
    if (!pathLength.ok()) return Result<void>::failure(pathLength.status());

    auto fromNode = network.nodeResult(edge.from);
    auto toNode   = network.nodeResult(edge.to);
    if (!fromNode.ok() || !toNode.ok())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "edge endpoints missing during bake", {}, {}, "procgen.road"));

    auto resolvePlan = [&](const RoadNode& node) -> JunctionPlan {
        if (planCache) {
            const auto found = planCache->find(node.id);
            if (found != planCache->end()) return found->second;
        }
        return planJunction(network, node);
    };
    // Own plans for the lifetime of arm pointers used below (no dangling locals).
    const JunctionPlan fromPlan = resolvePlan(fromNode.value());
    const JunctionPlan toPlan   = resolvePlan(toNode.value());

    // Trim each end at the curb-return tangency distance (equals junctionRadius
    // for orthogonal crosses; shorter for obtuse Y corners so the loft tip
    // docks flush with the fillet arc). Cap by half the edge so short links
    // still leave a mid strip.
    const float maxTrim = std::max(0.f, pathLength.value() * 0.5f - 0.5f);
    const float trimStart =
        std::min(std::max(0.f, trimFromPlan(fromPlan, fromNode.value(), edge.id)), maxTrim);
    const float trimEnd = std::min(std::max(0.f, trimFromPlan(toPlan, toNode.value(), edge.id)), maxTrim);
    if (trimStart + trimEnd >= pathLength.value() - 0.5f)
        return Result<void>::success();  // fully inside junction; skip strip

    const int segments = std::max(4, options.pathSegmentsPerEdge);
    auto      frames   = spline.value().sampleFramesResult(segments, true, 0.f, 24);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    auto findArmIn = [&](const JunctionPlan& plan, bool tipAtTo) -> const JunctionArm* {
        if (!plan.ok) return nullptr;
        for (const auto& arm : plan.arms) {
            if (arm.edgeId == edge.id && arm.tipAtTo == tipAtTo) return &arm;
        }
        return nullptr;
    };

    // Snap tip XZ/lateral axes to the junction plan while preserving sampled
    // elevation and up so graded/ramp arms do not form a step at the dock.
    auto snapTipToJunction = [&](SplineFrameSample& tip, const RoadNode& node, const JunctionPlan& plan,
                                 bool tipAtTo) {
        if (!plan.ok) return;
        for (const auto& arm : plan.arms) {
            if (arm.edgeId != edge.id || arm.tipAtTo != tipAtTo) continue;
            const float keepY    = tip.sample.y;
            const V3    keepUp   = normalize(V3{tip.upX, tip.upY, tip.upZ});
            const float keepFwdY = tip.forwardY;
            tip.sample.x         = node.x + arm.outDir.x * arm.trimDist;
            tip.sample.y         = keepY;
            tip.sample.z         = node.z + arm.outDir.z * arm.trimDist;
            V3 fwdH              = tipAtTo ? (arm.outDir * -1.f) : arm.outDir;
            V3 fwd               = normalize(V3{fwdH.x, keepFwdY, fwdH.z});
            if (length(V3{fwd.x, 0.f, fwd.z}) < 1e-4f) fwd = normalize(V3{fwdH.x, 0.f, fwdH.z});
            V3 side = normalize(cross(keepUp, fwd));
            if (length(side) < 1e-4f) side = normalize(cross(V3{0.f, 1.f, 0.f}, fwd));
            tip.forwardX = fwd.x;
            tip.forwardY = fwd.y;
            tip.forwardZ = fwd.z;
            tip.sideX    = side.x;
            tip.sideY    = side.y;
            tip.sideZ    = side.z;
            tip.upX      = keepUp.x;
            tip.upY      = keepUp.y;
            tip.upZ      = keepUp.z;
            return;
        }
    };

    auto makeDockFrame = [&](const RoadNode& node, const JunctionArm& arm, float alongFromHub, bool tipAtTo,
                             float keepY, float keepFwdY, V3 keepUp) {
        SplineFrameSample tip{};
        tip.sample.x                  = node.x + arm.outDir.x * alongFromHub;
        tip.sample.y                  = keepY;
        tip.sample.z                  = node.z + arm.outDir.z * alongFromHub;
        tip.sample.normalizedDistance = tipAtTo ? 1.f : 0.f;
        V3 fwdH                       = tipAtTo ? (arm.outDir * -1.f) : arm.outDir;
        V3 fwd                        = normalize(V3{fwdH.x, keepFwdY, fwdH.z});
        if (length(V3{fwd.x, 0.f, fwd.z}) < 1e-4f) fwd = normalize(V3{fwdH.x, 0.f, fwdH.z});
        V3 side = normalize(cross(keepUp, fwd));
        if (length(side) < 1e-4f) side = normalize(cross(V3{0.f, 1.f, 0.f}, fwd));
        tip.forwardX = fwd.x;
        tip.forwardY = fwd.y;
        tip.forwardZ = fwd.z;
        tip.sideX    = side.x;
        tip.sideY    = side.y;
        tip.sideZ    = side.z;
        tip.upX      = keepUp.x;
        tip.upY      = keepUp.y;
        tip.upZ      = keepUp.z;
        return tip;
    };

    // Keep mid frames, then force exact trim endpoints so the loft meets the
    // junction apron. When bend-to-dock nudged the tip direction, drop mid
    // frames inside the bend window and insert a short approach so the strip
    // curves from the authored spline onto the dock ray.
    auto appendBentApproach = [&](std::vector<SplineFrameSample>& out, const RoadNode& node,
                                  const JunctionPlan& plan, bool tipAtTo, float trimDist) -> Result<void> {
        const JunctionArm* arm = findArmIn(plan, tipAtTo);
        if (!arm || !arm->bent) {
            auto tip = spline.value().travelFrameResult(tipAtTo ? (pathLength.value() - trimDist) : trimDist,
                                                        "clamp", 24);
            if (!tip.ok()) return Result<void>::failure(tip.status());
            auto sample = std::move(tip).takeValue();
            snapTipToJunction(sample, node, plan, tipAtTo);
            out.push_back(std::move(sample));
            return Result<void>::success();
        }
        // Use the edge-capped trim for dock placement so short edges stay
        // consistent with the mid-frame window and maxTrim overlap check.
        const float dockTrim = trimDist;
        const float bendLen  = bendLength(arm->trimDist, trimDist);
        const float tipAlong = tipAtTo ? (pathLength.value() - trimDist) : trimDist;
        const float approach = tipAtTo ? (tipAlong - bendLen) : (tipAlong + bendLen);
        auto        approachFrame = spline.value().travelFrameResult(
            std::clamp(approach, 0.f, pathLength.value()), "clamp", 24);
        if (!approachFrame.ok()) return Result<void>::failure(approachFrame.status());
        auto        approachVal = std::move(approachFrame).takeValue();
        const float keepY       = approachVal.sample.y;
        const float keepFwdY    = approachVal.forwardY;
        const V3    keepUp      = normalize(V3{approachVal.upX, approachVal.upY, approachVal.upZ});
        const float a1          = dockTrim + bendLen * 0.55f;
        const float a2          = dockTrim;
        if (!tipAtTo) {
            // Tip first (near hub), then curve outward onto the authored approach.
            out.push_back(makeDockFrame(node, *arm, a2, tipAtTo, keepY, keepFwdY, keepUp));
            out.push_back(makeDockFrame(node, *arm, a1, tipAtTo, keepY, keepFwdY, keepUp));
            out.push_back(std::move(approachVal));
        } else {
            out.push_back(std::move(approachVal));
            out.push_back(makeDockFrame(node, *arm, a1, tipAtTo, keepY, keepFwdY, keepUp));
            out.push_back(makeDockFrame(node, *arm, a2, tipAtTo, keepY, keepFwdY, keepUp));
        }
        return Result<void>::success();
    };

    std::vector<SplineFrameSample> trimmed;
    trimmed.reserve(frames.value().size() + 8);
    const JunctionArm* fromArm = findArmIn(fromPlan, false);
    const JunctionArm* toArm   = findArmIn(toPlan, true);
    const float fromBend = (fromArm && fromArm->bent) ? bendLength(fromArm->trimDist, trimStart) : 0.f;
    const float toBend   = (toArm && toArm->bent) ? bendLength(toArm->trimDist, trimEnd) : 0.f;

    if (trimStart > 1e-3f) {
        auto bent = appendBentApproach(trimmed, fromNode.value(), fromPlan, false, trimStart);
        if (!bent.ok()) return bent;
    }
    const float endDist  = pathLength.value() - trimEnd;
    const float midStart = trimStart + fromBend + 1e-3f;
    const float midEnd   = endDist - toBend - 1e-3f;
    for (const auto& frame : frames.value()) {
        const float d = frame.sample.normalizedDistance * pathLength.value();
        if (d > midStart && d < midEnd) trimmed.push_back(frame);
    }
    if (trimEnd > 1e-3f) {
        auto bent = appendBentApproach(trimmed, toNode.value(), toPlan, true, trimEnd);
        if (!bent.ok()) return bent;
    }
    if (trimmed.size() < 2) return Result<void>::success();

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    auto lofted = loftProfileClean(mesh, trimmed, profile.value(), edge.style.uvMeters);
    if (!lofted.ok()) return lofted;
    // Cap stub ends. Skip caps when the tip docks into a planned curb-return
    // fillet — a shoulder fan there fights the fillet and reads as a white bar.
    auto tipDocksToFillet = [&](const JunctionPlan& plan, bool tipAtTo) {
        if (!plan.ok) return false;
        for (const auto& arm : plan.arms) {
            if (arm.edgeId == edge.id && arm.tipAtTo == tipAtTo) {
                for (const auto& c : plan.corners) {
                    if (c.filleted) return true;
                }
            }
        }
        return false;
    };
    if (trimStart > 0.05f && !tipDocksToFillet(fromPlan, false)) {
        if (trimStart < 2.5f) capProfileRing(mesh, trimmed.front(), profile.value(), false);
        else
            capProfileShoulders(mesh, trimmed.front(), profile.value(), false);
    }
    if (trimEnd > 0.05f && !tipDocksToFillet(toPlan, true)) {
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
        auto emitLaneNav = [&](int lane, bool reverse) {
            const float lateral =
                laneCenterOffset(edge.style, edge.lanesForward, edge.lanesBackward, lane, reverse);
            RoadPolyline poly;
            poly.r     = reverse ? 1.f : 0.15f;
            poly.g     = reverse ? 0.55f : 0.9f;
            poly.b     = reverse ? 0.15f : 1.f;
            poly.width = options.navRibbonHalfWidth * 2.f;
            float traveled  = 0.f;
            float nextArrow = options.arrowSpacing * 0.5f;
            V3    prev{};
            bool  hasPrev = false;
            const int begin = reverse ? static_cast<int>(trimmed.size()) - 1 : 0;
            const int end   = reverse ? -1 : static_cast<int>(trimmed.size());
            const int step  = reverse ? -1 : 1;
            for (int fi = begin; fi != end; fi += step) {
                const auto& frame = trimmed[static_cast<std::size_t>(fi)];
                const V3    origin{frame.sample.x, frame.sample.y, frame.sample.z};
                const V3    side{frame.sideX, frame.sideY, frame.sideZ};
                const V3    up{frame.upX, frame.upY, frame.upZ};
                if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;
                const V3 pos = origin + side * lateral + up * 0.08f;
                poly.xyz.push_back(pos.x);
                poly.xyz.push_back(pos.y);
                poly.xyz.push_back(pos.z);
                if (hasPrev) {
                    const float stepLen = length(pos - prev);
                    if (std::isfinite(stepLen) && stepLen > 1e-5f && stepLen < 1.0e4f) {
                        traveled += stepLen;
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
        };
        for (int lane = 0; lane < edge.lanesForward; ++lane) emitLaneNav(lane, false);
        for (int lane = 0; lane < edge.lanesBackward; ++lane) emitLaneNav(lane, true);
    }
    return Result<void>::success();
}

Result<void> bakeJunction(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                          const JunctionPlan* cachedPlan = nullptr) {
    const JunctionPlan plan = cachedPlan ? *cachedPlan : planJunction(network, node);
    if (plan.arms.size() < 2) return Result<void>::success();

    const float y        = node.y;
    const float asphaltY = y + 0.01f;
    const V3    hub{node.x, asphaltY, node.z};
    const V3    upN{0.f, 1.f, 0.f};
    const int   segs = 16;

    float avgCurbW = 0.f, avgWalkW = 0.f, curbH = 0.45f, walkH = 0.14f;
    for (const auto& a : plan.arms) {
        avgCurbW += a.curbWidth;
        avgWalkW += a.sidewalkWidth;
        curbH = a.curbHeight;
        walkH = a.sidewalkH;
    }
    avgCurbW /= static_cast<float>(plan.arms.size());
    avgWalkW /= static_cast<float>(plan.arms.size());
    const float curbW   = std::max(0.2f, avgCurbW);
    const float walkW   = std::max(0.35f, avgWalkW);
    const float walkY   = y + walkH;
    const float curbTop = y + curbH;
    const float jr      = node.junctionRadius;

    auto emitDiscFallback = [&]() {
        float maxHalfWidth = 0.f;
        for (const auto& a : plan.arms) maxHalfWidth = std::max(maxHalfWidth, a.halfWidth);
        const float radius = std::sqrt(jr * jr + maxHalfWidth * maxHalfWidth) + 0.35f;
        const int   nseg   = 32;
        for (int i = 0; i < nseg; ++i) {
            const float a0 = static_cast<float>(i) * 6.2831853f / static_cast<float>(nseg);
            const float a1 = static_cast<float>(i + 1) * 6.2831853f / static_cast<float>(nseg);
            appendOrientedTri(mesh, hub,
                              V3{node.x + std::cos(a0) * radius, asphaltY, node.z + std::sin(a0) * radius},
                              V3{node.x + std::cos(a1) * radius, asphaltY, node.z + std::sin(a1) * radius}, upN,
                              RoadMaterial::Asphalt);
        }
    };

    if (!plan.ok) {
        emitDiscFallback();
    } else {
        // Asphalt rim: tip chord (right tan → left tan) then curb-return arc.
        std::vector<std::pair<float, float>> rim;
        rim.reserve(plan.arms.size() * static_cast<std::size_t>(segs + 4));
        auto pushPt = [&](float x, float z) {
            if (!rim.empty() && std::fabs(rim.back().first - x) < 1e-4f &&
                std::fabs(rim.back().second - z) < 1e-4f)
                return;
            rim.emplace_back(x, z);
        };

        for (std::size_t i = 0; i < plan.arms.size(); ++i) {
            const JunctionCorner& prev = plan.corners[(i + plan.arms.size() - 1) % plan.arms.size()];
            const JunctionCorner& C    = plan.corners[i];
            // Use true tangency points (not tip±ah) so the apron rim matches the fillet.
            if (prev.filleted) pushPt(prev.bTan.x, prev.bTan.z);
            else {
                const JunctionArm& A = plan.arms[i];
                const V3 tip         = V3{node.x, 0.f, node.z} + A.outDir * A.trimDist;
                const V3 rLip        = tip + A.right * A.asphaltHalf;
                pushPt(rLip.x, rLip.z);
            }
            if (C.filleted) {
                pushPt(C.aTan.x, C.aTan.z);
                const int n = std::max(4, static_cast<int>(std::ceil(std::fabs(C.sweep) / 1.5707963f * segs)));
                for (int s = 1; s <= n; ++s) {
                    const float t   = static_cast<float>(s) / static_cast<float>(n);
                    const float ang = C.startAng + C.sweep * t;
                    pushPt(C.center.x + std::cos(ang) * C.radius, C.center.z + std::sin(ang) * C.radius);
                }
            } else {
                const JunctionArm& A = plan.arms[i];
                const V3 tip         = V3{node.x, 0.f, node.z} + A.outDir * A.trimDist;
                const V3 lLip        = tip - A.right * A.asphaltHalf;
                pushPt(lLip.x, lLip.z);
                pushPt(C.bTan.x, C.bTan.z);
            }
        }

        for (std::size_t i = 0; i < rim.size(); ++i) {
            const auto& p0 = rim[i];
            const auto& p1 = rim[(i + 1) % rim.size()];
            appendOrientedTri(mesh, hub, V3{p0.first, asphaltY, p0.second}, V3{p1.first, asphaltY, p1.second},
                              upN, RoadMaterial::Asphalt);
        }

        // Seal strips under each docked tip (wide enough to hide tip/apron float error).
        const float seal  = 0.55f;
        const float ySeal = asphaltY + 0.002f;
        for (const auto& A : plan.arms) {
            const V3 tip = V3{node.x, 0.f, node.z} + A.outDir * A.trimDist;
            const V3 r0  = tip + A.right * A.asphaltHalf - A.outDir * seal;
            const V3 r1  = tip + A.right * A.asphaltHalf + A.outDir * seal;
            const V3 l1  = tip - A.right * A.asphaltHalf + A.outDir * seal;
            const V3 l0  = tip - A.right * A.asphaltHalf - A.outDir * seal;
            appendOrientedQuad(mesh, V3{r0.x, ySeal, r0.z}, V3{r1.x, ySeal, r1.z}, V3{l1.x, ySeal, l1.z},
                               V3{l0.x, ySeal, l0.z}, upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Asphalt);

            // Curb/sidewalk docking ribbon: covers the tip shoulder so arm loft and
            // fillet rings share a continuous top (hides lateral step at 120° docks).
            // Use this arm's own profile dimensions so mixed styles still join cleanly.
            const float armCurbW   = std::max(0.2f, A.curbWidth);
            const float armWalkY   = y + A.sidewalkH;
            const float armCurbTop = y + A.curbHeight;
            for (int sideSign : {-1, 1}) {
                const float s   = static_cast<float>(sideSign);
                const V3    lat = A.right * s;
                const V3    a0  = tip + lat * A.asphaltHalf - A.outDir * 0.08f;
                const V3    a1  = tip + lat * A.asphaltHalf + A.outDir * 0.08f;
                const V3    c0  = tip + lat * (A.asphaltHalf + armCurbW) - A.outDir * 0.08f;
                const V3    c1  = tip + lat * (A.asphaltHalf + armCurbW) + A.outDir * 0.08f;
                const V3    w0  = tip + lat * A.halfWidth - A.outDir * 0.08f;
                const V3    w1  = tip + lat * A.halfWidth + A.outDir * 0.08f;
                appendOrientedQuad(mesh, V3{a0.x, armCurbTop, a0.z}, V3{c0.x, armCurbTop, c0.z},
                                   V3{c1.x, armCurbTop, c1.z}, V3{a1.x, armCurbTop, a1.z}, upN, 0.f, 1.f, 0.f, 1.f,
                                   RoadMaterial::Curb);
                appendOrientedQuad(mesh, V3{c0.x, armWalkY, c0.z}, V3{w0.x, armWalkY, w0.z}, V3{w1.x, armWalkY, w1.z},
                                   V3{c1.x, armWalkY, c1.z}, upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Sidewalk);
            }
        }

        auto wall = [&](V3 p0, V3 p1, float y0, float y1, V3 n, RoadMaterial mat) {
            appendOrientedQuad(mesh, V3{p0.x, y0, p0.z}, V3{p1.x, y0, p1.z}, V3{p1.x, y1, p1.z},
                               V3{p0.x, y1, p0.z}, n, 0.f, 1.f, 0.f, 1.f, mat);
            appendOrientedQuad(mesh, V3{p0.x, y0, p0.z}, V3{p1.x, y0, p1.z}, V3{p1.x, y1, p1.z},
                               V3{p0.x, y1, p0.z}, n * -1.f, 0.f, 1.f, 0.f, 1.f, mat);
        };

        auto emitBand = [&](V3 c, float r0, float r1, float r2, float startAng, float sweep) {
            const int n = std::max(4, static_cast<int>(std::ceil(std::fabs(sweep) / 1.5707963f * segs)));
            auto      arcPt = [&](float radius, float t) {
                const float ang = startAng + sweep * t;
                return V3{c.x + std::cos(ang) * radius, 0.f, c.z + std::sin(ang) * radius};
            };
            for (int s = 0; s < n; ++s) {
                const float t0  = static_cast<float>(s) / static_cast<float>(n);
                const float t1  = static_cast<float>(s + 1) / static_cast<float>(n);
                const V3    a0  = arcPt(r0, t0);
                const V3    a1  = arcPt(r0, t1);
                const V3    b0  = arcPt(r1, t0);
                const V3    b1  = arcPt(r1, t1);
                const V3    s0  = arcPt(r2, t0);
                const V3    s1  = arcPt(r2, t1);
                const V3    c0  = arcPt(std::max(0.05f, r2 - 0.06f), t0);
                const V3    c1  = arcPt(std::max(0.05f, r2 - 0.06f), t1);
                const V3    b0i = arcPt(r1 - 0.02f, t0);
                const V3    b1i = arcPt(r1 - 0.02f, t1);
                const V3    b0o = arcPt(r1 + 0.02f, t0);
                const V3    b1o = arcPt(r1 + 0.02f, t1);
                const V3 inToCorner = normalize(V3{c.x - a0.x, 0.f, c.z - a0.z});
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
        };

        auto emitStraightBand = [&](V3 aLip, V3 bLip) {
            V3 along = V3{bLip.x - aLip.x, 0.f, bLip.z - aLip.z};
            if (length(along) < 1e-4f) return;
            along      = normalize(along);
            V3 outward = normalize(cross(along, V3{0.f, 1.f, 0.f}));
            const V3 mid{(aLip.x + bLip.x) * 0.5f, 0.f, (aLip.z + bLip.z) * 0.5f};
            if (dot(outward, V3{mid.x - node.x, 0.f, mid.z - node.z}) < 0.f) outward = outward * -1.f;

            const V3 a0 = aLip;
            const V3 b0 = bLip;
            const V3 a1 = aLip + outward * curbW;
            const V3 b1 = bLip + outward * curbW;
            const V3 a2 = aLip + outward * (curbW + walkW);
            const V3 b2 = bLip + outward * (curbW + walkW);
            const V3 a3 = aLip + outward * (curbW + walkW + 0.06f);
            const V3 b3 = bLip + outward * (curbW + walkW + 0.06f);

            appendOrientedQuad(mesh, V3{a0.x, curbTop, a0.z}, V3{a1.x, curbTop, a1.z}, V3{b1.x, curbTop, b1.z},
                               V3{b0.x, curbTop, b0.z}, upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Curb);
            appendOrientedQuad(mesh, V3{a1.x, walkY, a1.z}, V3{a3.x, walkY, a3.z}, V3{b3.x, walkY, b3.z},
                               V3{b1.x, walkY, b1.z}, upN, 0.f, 1.f, 0.f, 1.f, RoadMaterial::Sidewalk);
            wall(a0, b0, y, curbTop, outward * -1.f, RoadMaterial::Curb);
            wall(a1, b1, walkY, curbTop, outward * -1.f, RoadMaterial::Curb);
            wall(a2, b2, y, walkY, outward * -1.f, RoadMaterial::Sidewalk);
        };

        for (std::size_t i = 0; i < plan.corners.size(); ++i) {
            const auto& C = plan.corners[i];
            if (C.filleted) {
                const float r0 = C.radius;
                const float r1 = std::max(0.2f, C.radius - curbW);
                const float r2 = std::max(0.15f, C.radius - curbW - walkW);
                emitBand(C.center, r0, r1, r2, C.startAng, C.sweep);
            } else {
                emitStraightBand(C.aTan, C.bTan);
            }
        }
    }

    // Zebra stripes just outside each docked tip.
    for (const auto& arm : plan.arms) {
        auto edge = network.edgeResult(arm.edgeId);
        if (!edge.ok()) continue;
        auto spline = edgeToSpline(edge.value());
        if (!spline.ok()) continue;
        auto len = spline.value().lengthResult(16);
        if (!len.ok() || len.value() < arm.trimDist + 1.5f) continue;
        const float alongDist =
            arm.tipAtTo ? (len.value() - arm.trimDist - 1.6f) : (arm.trimDist + 1.6f);
        auto frame = spline.value().travelFrameResult(alongDist, "clamp", 16);
        if (!frame.ok()) continue;
        const auto& f = frame.value();
        const V3    origin{f.sample.x, f.sample.y, f.sample.z};
        const V3    side{f.sideX, f.sideY, f.sideZ};
        V3          fwd{f.forwardX, f.forwardY, f.forwardZ};
        if (!arm.tipAtTo) fwd = fwd * -1.f;
        const V3    nrm{0.f, 1.f, 0.f};
        const float stripeW =
            edge.value().style.laneWidth *
            static_cast<float>(edge.value().lanesForward + edge.value().lanesBackward) * 0.45f;
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
                              const RoadBakeOptions& options,
                              const std::unordered_map<std::uint32_t, JunctionPlan>* planCache = nullptr) {
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

        JunctionPlan plan;
        if (planCache) {
            const auto found = planCache->find(node.value().id);
            plan             = found != planCache->end() ? found->second : planJunction(network, node.value());
        } else {
            plan = planJunction(network, node.value());
        }
        const float inTrim  = trimFromPlan(plan, node.value(), link.inEdge);
        const float outTrim = trimFromPlan(plan, node.value(), link.outEdge);
        const float inDist  = std::max(0.f, inLen.value() - inTrim);
        const float outDist = std::min(outTrim, outLen.value());
        auto        inFrame = inPath.travelFrameResult(inDist, "clamp", 16);
        auto        outFrame = outPath.travelFrameResult(outDist, "clamp", 16);
        if (!inFrame.ok() || !outFrame.ok()) continue;

        const float inLat =
            laneCenterOffset(inEdge.value().style, inEdge.value().lanesForward, inEdge.value().lanesBackward,
                             link.inLane, false);
        const float outLat =
            laneCenterOffset(outEdge.value().style, outEdge.value().lanesForward, outEdge.value().lanesBackward,
                             link.outLane, false);

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
    if (name == "decorTrunk") return {0.42f, 0.28f, 0.16f, 1.f};
    if (name == "decorFoliage") return {0.22f, 0.55f, 0.24f, 1.f};
    if (name == "decorGrass") return {0.28f, 0.58f, 0.26f, 1.f};
    if (name == "decorMedian") return {0.72f, 0.72f, 0.70f, 1.f};
    if (name == "decorMetal") return {0.55f, 0.58f, 0.62f, 1.f};
    if (name == "decorLamp") return {0.95f, 0.90f, 0.55f, 1.f};
    if (name == "decorPole") return {0.48f, 0.42f, 0.34f, 1.f};
    if (name == "decorCustom") return {0.70f, 0.55f, 0.40f, 1.f};
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
        return Result<RoadBakeResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "segment counts must be >= 2", {}, {}, "procgen.road"));
    if (network.edgeCount() == 0)
        return Result<RoadBakeResult>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                 "road network has no edges", {}, {}, "procgen.road"));

    // Plan every junction once and reuse for edge trim/snap, apron mesh and turns.
    std::unordered_map<std::uint32_t, JunctionPlan> plans;
    plans.reserve(static_cast<std::size_t>(network.nodeCount()));
    for (const auto& node : network.nodes()) plans.emplace(node.id, planJunction(network, node));

    RoadBakeResult result;
    for (const auto& edge : network.edges()) {
        auto baked = bakeEdgeGeometry(result.mesh, result.overlay, network, edge, options, &plans);
        if (!baked.ok()) return Result<RoadBakeResult>::failure(baked.status());
    }
    if (options.includeJunctions) {
        for (const auto& node : network.nodes()) {
            const auto found = plans.find(node.id);
            auto       junction =
                bakeJunction(result.mesh, network, node, found != plans.end() ? &found->second : nullptr);
            if (!junction.ok()) return Result<RoadBakeResult>::failure(junction.status());
        }
    }
    if (options.includeNavigation) {
        auto turns = bakeTurnOverlays(result.mesh, result.overlay, network, options, &plans);
        if (!turns.ok()) return Result<RoadBakeResult>::failure(turns.status());
    }
    {
        auto decor = bakeRoadDecorations(result.mesh, network, options.decor);
        if (!decor.ok()) return Result<RoadBakeResult>::failure(decor.status());
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
        return Result<RoadBakeResult>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation, "bake produced an empty mesh", {}, {}, "procgen.road"));
    return Result<RoadBakeResult>::success(std::move(result));
}

}  // namespace eve::procgen::road
