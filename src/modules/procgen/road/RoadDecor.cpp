#include "procgen/road/RoadDecor.h"

#include "common/Diagnostic.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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

std::uint32_t mixSeed(std::uint32_t a, std::uint32_t b) {
    std::uint32_t x = a + 0x9e3779b9u + (b << 6u) + (b >> 2u);
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float rand01(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>((state >> 8u) & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

void appendOrientedQuad(MeshBuild& mesh, V3 a, V3 b, V3 c, V3 d, V3 normal, const std::string& group) {
    mesh.setActiveGroup(group);
    const V3 n = normalize(normal);
    V3       p0 = a, p1 = b, p2 = c, p3 = d;
    if (dot(cross(b - a, c - a), n) < 0.f) {
        p0 = a;
        p1 = d;
        p2 = c;
        p3 = b;
    }
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(p0.x, p0.y, p0.z, n.x, n.y, n.z, 0.f, 0.f);
    mesh.addVertex(p1.x, p1.y, p1.z, n.x, n.y, n.z, 1.f, 0.f);
    mesh.addVertex(p2.x, p2.y, p2.z, n.x, n.y, n.z, 1.f, 1.f);
    mesh.addVertex(p3.x, p3.y, p3.z, n.x, n.y, n.z, 0.f, 1.f);
    mesh.addTriangle(base, base + 1, base + 2);
    mesh.addTriangle(base, base + 2, base + 3);
}

void appendBox(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float hx, float hy, float hz,
               const std::string& group) {
    mesh.setActiveGroup(group);
    const V3 corners[8] = {
        center + side * -hx + up * -hy + forward * -hz, center + side * hx + up * -hy + forward * -hz,
        center + side * hx + up * hy + forward * -hz,   center + side * -hx + up * hy + forward * -hz,
        center + side * -hx + up * -hy + forward * hz,  center + side * hx + up * -hy + forward * hz,
        center + side * hx + up * hy + forward * hz,    center + side * -hx + up * hy + forward * hz,
    };
    // Winding matches outward normals for a right-handed (side, up, forward) frame.
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    const V3 normals[6]   = {forward * -1.f, forward, up * -1.f, up, side * -1.f, side};
    for (int f = 0; f < 6; ++f) {
        appendOrientedQuad(mesh, corners[faces[f][0]], corners[faces[f][1]], corners[faces[f][2]],
                           corners[faces[f][3]], normals[f], group);
    }
}

void appendCylinder(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float radius, float halfHeight, int sides,
                    const std::string& group) {
    mesh.setActiveGroup(group);
    const int n = std::max(5, sides);
    std::vector<V3> ring(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(n);
        ring[static_cast<std::size_t>(i)] = side * (std::cos(a) * radius) + forward * (std::sin(a) * radius);
    }
    const V3 top = center + up * halfHeight;
    const V3 bot = center - up * halfHeight;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const V3  a = bot + ring[static_cast<std::size_t>(i)];
        const V3  b = bot + ring[static_cast<std::size_t>(j)];
        const V3  c = top + ring[static_cast<std::size_t>(j)];
        const V3  d = top + ring[static_cast<std::size_t>(i)];
        const V3  nrm = normalize(ring[static_cast<std::size_t>(i)] + ring[static_cast<std::size_t>(j)]);
        appendOrientedQuad(mesh, a, b, c, d, nrm, group);
    }
    // One fan per cap over the full ring; winding matches outward ±up.
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const V3  ri = top + ring[static_cast<std::size_t>(i)];
        const V3  rj = top + ring[static_cast<std::size_t>(j)];
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(top.x, top.y, top.z, up.x, up.y, up.z, 0.5f, 0.5f);
        mesh.addVertex(rj.x, rj.y, rj.z, up.x, up.y, up.z, 1.f, 0.f);
        mesh.addVertex(ri.x, ri.y, ri.z, up.x, up.y, up.z, 0.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
    }
    const V3 nd = up * -1.f;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const V3  ri = bot + ring[static_cast<std::size_t>(i)];
        const V3  rj = bot + ring[static_cast<std::size_t>(j)];
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(bot.x, bot.y, bot.z, nd.x, nd.y, nd.z, 0.5f, 0.5f);
        mesh.addVertex(ri.x, ri.y, ri.z, nd.x, nd.y, nd.z, 0.f, 0.f);
        mesh.addVertex(rj.x, rj.y, rj.z, nd.x, nd.y, nd.z, 1.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
    }
}

void appendCone(MeshBuild& mesh, V3 center, V3 side, V3 up, V3 forward, float radius, float halfHeight, int sides,
                const std::string& group) {
    mesh.setActiveGroup(group);
    const int n = std::max(5, sides);
    const V3 tip = center + up * halfHeight;
    const V3 bot = center - up * halfHeight;
    std::vector<V3> ring(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(n);
        ring[static_cast<std::size_t>(i)] = side * (std::cos(a) * radius) + forward * (std::sin(a) * radius);
    }
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const V3  a = bot + ring[static_cast<std::size_t>(i)];
        const V3  b = bot + ring[static_cast<std::size_t>(j)];
        // Outward side normal for right-handed (side, up, forward).
        const V3  nrm = normalize(cross(tip - a, b - a));
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(a.x, a.y, a.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        mesh.addVertex(tip.x, tip.y, tip.z, nrm.x, nrm.y, nrm.z, 0.5f, 1.f);
        mesh.addVertex(b.x, b.y, b.z, nrm.x, nrm.y, nrm.z, 1.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
    }
    // Closed base so crowns are opaque from below.
    const V3 nd = up * -1.f;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const V3  ri = bot + ring[static_cast<std::size_t>(i)];
        const V3  rj = bot + ring[static_cast<std::size_t>(j)];
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(bot.x, bot.y, bot.z, nd.x, nd.y, nd.z, 0.5f, 0.5f);
        mesh.addVertex(ri.x, ri.y, ri.z, nd.x, nd.y, nd.z, 0.f, 0.f);
        mesh.addVertex(rj.x, rj.y, rj.z, nd.x, nd.y, nd.z, 1.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
    }
}

V3 rotateAroundUp(V3 v, V3 up, float rad) {
    // Rodrigues around up.
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return v * c + cross(up, v) * s + up * (dot(up, v) * (1.f - c));
}

void emitPrimitive(MeshBuild& mesh, const RoadDecorPrimitive& part, V3 origin, V3 side, V3 up, V3 forward) {
    const V3 center = origin + side * part.ox + up * part.oy + forward * part.oz;
    switch (part.shape) {
        case RoadDecorPrimitive::Shape::Cylinder:
            appendCylinder(mesh, center, side, up, forward, std::max(0.02f, part.sx), std::max(0.02f, part.sy), 8,
                           part.group);
            break;
        case RoadDecorPrimitive::Shape::Cone:
            appendCone(mesh, center, side, up, forward, std::max(0.02f, part.sx), std::max(0.02f, part.sy), 8,
                       part.group);
            break;
        case RoadDecorPrimitive::Shape::Box:
        default:
            appendBox(mesh, center, side, up, forward, std::max(0.01f, part.sx), std::max(0.01f, part.sy),
                      std::max(0.01f, part.sz), part.group);
            break;
    }
}

Result<void> validateSpec(const RoadDecorSpec& spec) {
    if (spec.id.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor id must be non-empty", "decor"));
    if (!std::isfinite(spec.spacing) || spec.spacing < 0.5f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor spacing must be >= 0.5", "decor"));
    if (!std::isfinite(spec.startOffset) || spec.startOffset < 0.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor startOffset must be >= 0", "decor"));
    if (!std::isfinite(spec.junctionClearance) || spec.junctionClearance < 0.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor junctionClearance must be >= 0", "decor"));
    if (!std::isfinite(spec.lateralGap) || !std::isfinite(spec.lateral) || !std::isfinite(spec.yawJitterDeg) ||
        !std::isfinite(spec.lateralJitter))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor placement fields must be finite", "decor"));
    if (spec.parts.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor needs at least one primitive", "decor"));
    if (!spec.roadside && !spec.centerline)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "decor must set roadside and/or centerline", "decor"));
    for (const auto& p : spec.parts) {
        if (p.group.empty() || !std::isfinite(p.sx) || !std::isfinite(p.sy) || !std::isfinite(p.sz) ||
            !std::isfinite(p.ox) || !std::isfinite(p.oy) || !std::isfinite(p.oz))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "decor primitive invalid", "decor"));
    }
    return Result<void>::success();
}

/** @brief Skip past curb-return trim and bend-to-dock window at a junction tip. */
float endClearance(const RoadNetwork& network, std::uint32_t nodeId, float extra) {
    auto node = network.nodeResult(nodeId);
    if (!node.ok()) return 2.f + std::max(0.f, extra);
    const float jr = std::max(1.f, node.value().junctionRadius);
    // trimDist ≤ jr; bendLen ≤ max(2, 0.9·trim). Over-estimate is safe (fewer tip props).
    return jr + std::max(2.f, jr * 0.9f) + std::max(0.f, extra);
}

bool edgeIsElevated(const RoadEdge& edge) {
    // Bridge styles keep pierClearance finite; ground styles use a huge sentinel.
    if (edge.style.pierClearance < 40.f) return true;
    for (const auto& p : edge.controlPoints) {
        if (std::isfinite(p.y) && p.y > 0.75f) return true;
    }
    return false;
}

Result<void> placeSpecAlongEdge(MeshBuild& mesh, const RoadNetwork& network, const RoadEdge& edge,
                                const RoadDecorSpec& spec, std::uint32_t seed) {
    // Roadside props float beside elevated decks; centerline props may still sit on the deck.
    if (spec.roadside && !spec.centerline && edgeIsElevated(edge)) return Result<void>::success();

    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto len = spline.value().lengthResult(24);
    if (!len.ok()) return Result<void>::failure(len.status());

    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());
    const float halfW = profile.value().halfWidth;

    const float clear0 = endClearance(network, edge.from, spec.junctionClearance);
    const float clear1 = endClearance(network, edge.to, spec.junctionClearance);
    const float usable = len.value() - clear0 - clear1;
    if (usable < spec.spacing * 0.5f) return Result<void>::success();

    std::uint32_t rng = mixSeed(seed, mixSeed(edge.id, mixSeed(spec.seedSalt, 0xdec04u)));
    // Never start before the junction clear zone (negative startOffset is rejected upstream).
    float         d   = clear0 + spec.startOffset;
    if (d < clear0) d = clear0;
    int           idx = 0;
    while (d <= len.value() - clear1 - 0.05f) {
        auto frame = spline.value().travelFrameResult(d, "clamp", 24);
        if (!frame.ok()) return Result<void>::failure(frame.status());
        const auto& f = frame.value();
        V3 origin{f.sample.x, f.sample.y, f.sample.z};
        V3 side{f.sideX, f.sideY, f.sideZ};
        V3 up{f.upX, f.upY, f.upZ};
        V3 forward{f.forwardX, f.forwardY, f.forwardZ};
        if (std::fabs(spec.yawJitterDeg) > 1e-3f) {
            const float yaw = (rand01(rng) * 2.f - 1.f) * spec.yawJitterDeg * 0.01745329252f;
            side            = rotateAroundUp(side, up, yaw);
            forward         = rotateAroundUp(forward, up, yaw);
        }

        auto emitAt = [&](float lat, V3 localSide) {
            float jitter = 0.f;
            if (std::fabs(spec.lateralJitter) > 1e-4f) jitter = (rand01(rng) * 2.f - 1.f) * spec.lateralJitter;
            const V3 pos = origin + side * (lat + jitter);
            for (const auto& part : spec.parts) emitPrimitive(mesh, part, pos, localSide, up, forward);
        };

        if (spec.centerline) emitAt(spec.lateral, side);
        if (spec.roadside && !edgeIsElevated(edge)) {
            const float lat = halfW + spec.lateralGap + spec.lateral;
            emitAt(lat, side);
            // Mirror side basis so local ox (toward road for lights) stays inward.
            if (spec.bothSides) emitAt(-lat, side * -1.f);
        }

        d += spec.spacing;
        ++idx;
        if (idx > 4096) break;
    }
    return Result<void>::success();
}

Result<void> bakeGreenbelt(MeshBuild& mesh, const RoadNetwork& network, const RoadEdge& edge, float width) {
    if (width < 0.15f || edgeIsElevated(edge)) return Result<void>::success();
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto len = spline.value().lengthResult(16);
    if (!len.ok()) return Result<void>::failure(len.status());
    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    const float clear0 = endClearance(network, edge.from, 0.8f);
    const float clear1 = endClearance(network, edge.to, 0.8f);
    if (len.value() <= clear0 + clear1 + 1.f) return Result<void>::success();

    const float inner = profile.value().halfWidth;
    const float outer = inner + width;
    const float y     = 0.03f;
    const float step  = 2.5f;
    for (float d0 = clear0; d0 + 0.05f < len.value() - clear1; d0 += step) {
        const float d1 = std::min(d0 + step, len.value() - clear1);
        auto        f0 = spline.value().travelFrameResult(d0, "clamp", 16);
        auto        f1 = spline.value().travelFrameResult(d1, "clamp", 16);
        if (!f0.ok() || !f1.ok()) return Result<void>::failure(f0.ok() ? f1.status() : f0.status());
        for (float sign : {-1.f, 1.f}) {
            const V3 o0{f0.value().sample.x, f0.value().sample.y + y, f0.value().sample.z};
            const V3 o1{f1.value().sample.x, f1.value().sample.y + y, f1.value().sample.z};
            const V3 s0{f0.value().sideX, f0.value().sideY, f0.value().sideZ};
            const V3 s1{f1.value().sideX, f1.value().sideY, f1.value().sideZ};
            const V3 a = o0 + s0 * (sign * inner);
            const V3 b = o0 + s0 * (sign * outer);
            const V3 c = o1 + s1 * (sign * outer);
            const V3 d = o1 + s1 * (sign * inner);
            appendOrientedQuad(mesh, a, b, c, d, V3{0.f, 1.f, 0.f}, "decorGrass");
        }
    }
    return Result<void>::success();
}

Result<void> bakeMedianStrip(MeshBuild& mesh, const RoadNetwork& network, const RoadEdge& edge, float width,
                             float height) {
    if (edge.lanesBackward <= 0 || width < 0.2f) return Result<void>::success();
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto len = spline.value().lengthResult(16);
    if (!len.ok()) return Result<void>::failure(len.status());
    const float clear0 = endClearance(network, edge.from, 1.0f);
    const float clear1 = endClearance(network, edge.to, 1.0f);
    if (len.value() <= clear0 + clear1 + 1.f) return Result<void>::success();

    const float hw   = width * 0.5f;
    const float hy   = height * 0.5f;
    const float step = 2.5f;
    for (float d0 = clear0; d0 + 0.05f < len.value() - clear1; d0 += step) {
        const float d1 = std::min(d0 + step, len.value() - clear1);
        auto        f0 = spline.value().travelFrameResult(d0, "clamp", 16);
        auto        f1 = spline.value().travelFrameResult(d1, "clamp", 16);
        if (!f0.ok() || !f1.ok()) return Result<void>::failure(f0.ok() ? f1.status() : f0.status());
        const V3 o0{f0.value().sample.x, f0.value().sample.y + hy + 0.02f, f0.value().sample.z};
        const V3 o1{f1.value().sample.x, f1.value().sample.y + hy + 0.02f, f1.value().sample.z};
        const V3 mid = (o0 + o1) * 0.5f;
        V3       fwd = normalize(o1 - o0);
        V3       side{f0.value().sideX, f0.value().sideY, f0.value().sideZ};
        V3       up{0.f, 1.f, 0.f};
        const float segHalf = length(o1 - o0) * 0.5f;
        appendBox(mesh, mid, side, up, fwd, hw, hy, std::max(0.05f, segHalf), "decorMedian");
        appendOrientedQuad(mesh, mid + side * -hw + up * hy + fwd * -segHalf, mid + side * hw + up * hy + fwd * -segHalf,
                           mid + side * hw + up * hy + fwd * segHalf, mid + side * -hw + up * hy + fwd * segHalf, up,
                           "decorGrass");
    }
    return Result<void>::success();
}

}  // namespace

Result<void> addCustomRoadDecor(RoadDecorOptions& options, RoadDecorSpec spec) {
    auto ok = validateSpec(spec);
    if (!ok.ok()) return ok;
    for (const auto& existing : options.custom) {
        if (existing.id == spec.id)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::AlreadyExists, "decor id already registered", "decor"));
    }
    options.custom.push_back(std::move(spec));
    return Result<void>::success();
}

RoadDecorSpec makeStreetTreeDecor(float spacing) {
    RoadDecorSpec spec;
    spec.id                = "builtin.tree";
    spec.spacing           = spacing;
    spec.roadside          = true;
    spec.bothSides         = true;
    spec.centerline        = false;
    spec.lateralGap        = 0.85f;
    spec.yawJitterDeg      = 25.f;
    spec.lateralJitter     = 0.25f;
    spec.junctionClearance = 1.2f;
    RoadDecorPrimitive trunk;
    trunk.shape = RoadDecorPrimitive::Shape::Cylinder;
    trunk.oy    = 0.9f;
    trunk.sx    = 0.14f;
    trunk.sy    = 0.9f;
    trunk.group = "decorTrunk";
    RoadDecorPrimitive crown;
    crown.shape = RoadDecorPrimitive::Shape::Cone;
    crown.oy    = 2.35f;
    crown.sx    = 1.05f;
    crown.sy    = 1.1f;
    crown.group = "decorFoliage";
    RoadDecorPrimitive crownBall;
    crownBall.shape = RoadDecorPrimitive::Shape::Cylinder;
    crownBall.oy    = 2.9f;
    crownBall.sx    = 0.85f;
    crownBall.sy    = 0.55f;
    crownBall.group = "decorFoliage";
    spec.parts      = {trunk, crown, crownBall};
    return spec;
}

RoadDecorSpec makeStreetLightDecor(float spacing) {
    RoadDecorSpec spec;
    spec.id                = "builtin.streetLight";
    spec.spacing           = spacing;
    spec.roadside          = true;
    spec.bothSides         = true;
    spec.lateralGap        = 0.45f;
    spec.junctionClearance = 2.0f;
    spec.startOffset       = 3.f;
    RoadDecorPrimitive pole;
    pole.shape = RoadDecorPrimitive::Shape::Cylinder;
    pole.oy    = 2.4f;
    pole.sx    = 0.07f;
    pole.sy    = 2.4f;
    pole.group = "decorMetal";
    RoadDecorPrimitive arm;
    arm.shape = RoadDecorPrimitive::Shape::Box;
    arm.ox    = -0.55f;
    arm.oy    = 4.55f;
    arm.sx    = 0.55f;
    arm.sy    = 0.05f;
    arm.sz    = 0.05f;
    arm.group = "decorMetal";
    RoadDecorPrimitive lamp;
    lamp.shape = RoadDecorPrimitive::Shape::Box;
    lamp.ox    = -1.05f;
    lamp.oy    = 4.35f;
    lamp.sx    = 0.18f;
    lamp.sy    = 0.1f;
    lamp.sz    = 0.22f;
    lamp.group = "decorLamp";
    spec.parts = {pole, arm, lamp};
    return spec;
}

RoadDecorSpec makeUtilityPoleDecor(float spacing) {
    RoadDecorSpec spec;
    spec.id                = "builtin.utilityPole";
    spec.spacing           = spacing;
    spec.roadside          = true;
    spec.bothSides         = false;  // one side only
    spec.lateralGap        = 1.15f;
    spec.junctionClearance = 2.5f;
    spec.startOffset       = 4.f;
    RoadDecorPrimitive shaft;
    shaft.shape = RoadDecorPrimitive::Shape::Cylinder;
    shaft.oy    = 3.2f;
    shaft.sx    = 0.12f;
    shaft.sy    = 3.2f;
    shaft.group = "decorPole";
    RoadDecorPrimitive cross;
    cross.shape = RoadDecorPrimitive::Shape::Box;
    cross.oy    = 5.9f;
    cross.sx    = 0.9f;
    cross.sy    = 0.06f;
    cross.sz    = 0.06f;
    cross.group = "decorPole";
    spec.parts  = {shaft, cross};
    return spec;
}

Result<void> bakeRoadDecorations(MeshBuild& mesh, const RoadNetwork& network, const RoadDecorOptions& options) {
    std::vector<RoadDecorSpec> specs;
    specs.reserve(8 + options.custom.size());
    if (options.trees) specs.push_back(makeStreetTreeDecor(options.treeSpacing));
    if (options.streetLights) specs.push_back(makeStreetLightDecor(options.lightSpacing));
    if (options.utilityPoles) specs.push_back(makeUtilityPoleDecor(options.poleSpacing));
    for (const auto& custom : options.custom) specs.push_back(custom);

    // Validate every enabled spec (built-in spacing is caller-controlled).
    for (const auto& spec : specs) {
        auto ok = validateSpec(spec);
        if (!ok.ok()) return ok;
    }

    for (const auto& edge : network.edges()) {
        if (options.greenbelt) {
            auto g = bakeGreenbelt(mesh, network, edge, options.greenbeltWidth);
            if (!g.ok()) return g;
        }
        if (options.medianStrip) {
            auto m = bakeMedianStrip(mesh, network, edge, options.medianWidth, options.medianHeight);
            if (!m.ok()) return m;
        }
        for (const auto& spec : specs) {
            auto placed = placeSpecAlongEdge(mesh, network, edge, spec, options.seed);
            if (!placed.ok()) return placed;
        }
    }
    return Result<void>::success();
}

}  // namespace eve::procgen::road
