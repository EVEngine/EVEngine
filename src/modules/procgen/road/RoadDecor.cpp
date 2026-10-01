#include "procgen/road/RoadDecor.h"

#include "common/Diagnostic.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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
    for (int i = 1; i + 1 < n; ++i) {
        const V3 b = top + ring[0];
        const V3 c = top + ring[static_cast<std::size_t>(i)];
        const V3 d = top + ring[static_cast<std::size_t>(i + 1)];
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(top.x, top.y, top.z, up.x, up.y, up.z, 0.5f, 0.5f);
        mesh.addVertex(b.x, b.y, b.z, up.x, up.y, up.z, 0.f, 0.f);
        mesh.addVertex(c.x, c.y, c.z, up.x, up.y, up.z, 1.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
        const auto base2 = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(top.x, top.y, top.z, up.x, up.y, up.z, 0.5f, 0.5f);
        mesh.addVertex(c.x, c.y, c.z, up.x, up.y, up.z, 0.f, 0.f);
        mesh.addVertex(d.x, d.y, d.z, up.x, up.y, up.z, 1.f, 0.f);
        mesh.addTriangle(base2, base2 + 1, base2 + 2);
    }
    const V3 nd = up * -1.f;
    for (int i = 1; i + 1 < n; ++i) {
        const V3 b = bot + ring[0];
        const V3 c = bot + ring[static_cast<std::size_t>(i)];
        const V3 d = bot + ring[static_cast<std::size_t>(i + 1)];
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(bot.x, bot.y, bot.z, nd.x, nd.y, nd.z, 0.5f, 0.5f);
        mesh.addVertex(c.x, c.y, c.z, nd.x, nd.y, nd.z, 0.f, 0.f);
        mesh.addVertex(b.x, b.y, b.z, nd.x, nd.y, nd.z, 1.f, 0.f);
        mesh.addTriangle(base, base + 1, base + 2);
        const auto base2 = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(bot.x, bot.y, bot.z, nd.x, nd.y, nd.z, 0.5f, 0.5f);
        mesh.addVertex(d.x, d.y, d.z, nd.x, nd.y, nd.z, 0.f, 0.f);
        mesh.addVertex(c.x, c.y, c.z, nd.x, nd.y, nd.z, 1.f, 0.f);
        mesh.addTriangle(base2, base2 + 1, base2 + 2);
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
        const V3  nrm = normalize(cross(b - a, tip - a));
        const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
        mesh.addVertex(a.x, a.y, a.z, nrm.x, nrm.y, nrm.z, 0.f, 0.f);
        mesh.addVertex(b.x, b.y, b.z, nrm.x, nrm.y, nrm.z, 1.f, 0.f);
        mesh.addVertex(tip.x, tip.y, tip.z, nrm.x, nrm.y, nrm.z, 0.5f, 1.f);
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
    if (spec.parts.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "decor needs at least one primitive", "decor"));
    if (!spec.roadside && !spec.centerline)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "decor must set roadside and/or centerline", "decor"));
    for (const auto& p : spec.parts) {
        if (p.group.empty() || !std::isfinite(p.sx) || !std::isfinite(p.sy) || !std::isfinite(p.sz))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "decor primitive invalid", "decor"));
    }
    return Result<void>::success();
}

float endClearance(const RoadNetwork& network, std::uint32_t nodeId, float extra) {
    auto node = network.nodeResult(nodeId);
    if (!node.ok()) return 2.f + extra;
    return std::max(1.f, node.value().junctionRadius) + extra;
}

Result<void> placeSpecAlongEdge(MeshBuild& mesh, const RoadNetwork& network, const RoadEdge& edge,
                                const RoadDecorSpec& spec, std::uint32_t seed) {
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
    float         d   = clear0 + spec.startOffset;
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

        auto emitAt = [&](float lat) {
            float jitter = 0.f;
            if (std::fabs(spec.lateralJitter) > 1e-4f) jitter = (rand01(rng) * 2.f - 1.f) * spec.lateralJitter;
            const V3 pos = origin + side * (lat + jitter);
            for (const auto& part : spec.parts) emitPrimitive(mesh, part, pos, side, up, forward);
        };

        if (spec.centerline) emitAt(spec.lateral);
        if (spec.roadside) {
            const float lat = halfW + spec.lateralGap + spec.lateral;
            emitAt(lat);
            if (spec.bothSides) emitAt(-lat);
        }

        d += spec.spacing;
        ++idx;
        if (idx > 4096) break;
    }
    return Result<void>::success();
}

Result<void> bakeGreenbelt(MeshBuild& mesh, const RoadNetwork& network, const RoadEdge& edge, float width) {
    if (width < 0.15f) return Result<void>::success();
    auto spline = edgeToSpline(edge);
    if (!spline.ok()) return Result<void>::failure(spline.status());
    auto len = spline.value().lengthResult(16);
    if (!len.ok()) return Result<void>::failure(len.status());
    auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
    if (!profile.ok()) return Result<void>::failure(profile.status());

    const float clear0 = endClearance(network, edge.from, 0.8f);
    const float clear1 = endClearance(network, edge.to, 0.8f);
    if (len.value() <= clear0 + clear1 + 1.f) return Result<void>::success();

    const int segs = std::max(4, static_cast<int>(len.value() / 2.5f));
    auto frames = spline.value().sampleFramesResult(segs, true, 0.f, 16);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    const float inner = profile.value().halfWidth;
    const float outer = inner + width;
    const float y     = 0.03f;
    for (std::size_t i = 0; i + 1 < frames.value().size(); ++i) {
        const auto& f0 = frames.value()[i];
        const auto& f1 = frames.value()[i + 1];
        const float d0 = f0.sample.normalizedDistance * len.value();
        const float d1 = f1.sample.normalizedDistance * len.value();
        if (d1 < clear0 || d0 > len.value() - clear1) continue;
        for (float sign : {-1.f, 1.f}) {
            const V3 o0{f0.sample.x, f0.sample.y + y, f0.sample.z};
            const V3 o1{f1.sample.x, f1.sample.y + y, f1.sample.z};
            const V3 s0{f0.sideX, f0.sideY, f0.sideZ};
            const V3 s1{f1.sideX, f1.sideY, f1.sideZ};
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

    const int segs = std::max(4, static_cast<int>(len.value() / 2.5f));
    auto frames = spline.value().sampleFramesResult(segs, true, 0.f, 16);
    if (!frames.ok()) return Result<void>::failure(frames.status());

    const float hw = width * 0.5f;
    const float hy = height * 0.5f;
    for (std::size_t i = 0; i + 1 < frames.value().size(); ++i) {
        const auto& f0 = frames.value()[i];
        const auto& f1 = frames.value()[i + 1];
        const float d0 = f0.sample.normalizedDistance * len.value();
        const float d1 = f1.sample.normalizedDistance * len.value();
        if (d1 < clear0 || d0 > len.value() - clear1) continue;
        const V3 o0{f0.sample.x, f0.sample.y + hy + 0.02f, f0.sample.z};
        const V3 o1{f1.sample.x, f1.sample.y + hy + 0.02f, f1.sample.z};
        const V3 mid = (o0 + o1) * 0.5f;
        V3       fwd = normalize(o1 - o0);
        V3       side{f0.sideX, f0.sideY, f0.sideZ};
        V3       up{0.f, 1.f, 0.f};
        const float segHalf = length(o1 - o0) * 0.5f;
        appendBox(mesh, mid, side, up, fwd, hw, hy, std::max(0.05f, segHalf), "decorMedian");
        // Grass cap on median.
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
    for (const auto& custom : options.custom) {
        auto ok = validateSpec(custom);
        if (!ok.ok()) return ok;
        specs.push_back(custom);
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
