#include "procgen/mesh/MeshContactBlend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::procgen {
namespace {

constexpr float kEpsilon = 1e-8f;

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length2(Vec3 v) { return dot(v, v); }
Vec3 normalized(Vec3 v) {
    const float len = std::sqrt(length2(v));
    return len > kEpsilon ? v * (1.f / len) : Vec3{0.f, 1.f, 0.f};
}
float saturate(float value) { return std::clamp(value, 0.f, 1.f); }

float evaluateFalloff(std::string_view kind, float t) {
    t = saturate(t);
    if (kind == "linear") return t;
    if (kind == "sharp") return t * t;
    if (kind == "sphere") return std::sqrt(std::max(0.f, 2.f * t - t * t));
    return t * t * (3.f - 2.f * t);
}

struct ClosestHit {
    float distance = std::numeric_limits<float>::infinity();
    Vec3  point{};
    Vec3  normal{0.f, 1.f, 0.f};
};

ClosestHit closestOnTriangle(Vec3 point, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 ab = b - a, ac = c - a, ap = point - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    const Vec3 faceNormal = normalized(cross(ab, ac));
    if (d1 <= 0.f && d2 <= 0.f) return {std::sqrt(length2(point - a)), a, faceNormal};
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return {std::sqrt(length2(point - b)), b, faceNormal};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float v = d1 / (d1 - d3);
        const Vec3  p = a + ab * v;
        return {std::sqrt(length2(point - p)), p, faceNormal};
    }
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return {std::sqrt(length2(point - c)), c, faceNormal};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float w = d2 / (d2 - d6);
        const Vec3  p = a + ac * w;
        return {std::sqrt(length2(point - p)), p, faceNormal};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const Vec3  p = b + (c - b) * w;
        return {std::sqrt(length2(point - p)), p, faceNormal};
    }
    const float denom = 1.f / (va + vb + vc);
    const float v     = vb * denom;
    const float w     = vc * denom;
    const Vec3  p     = a + ab * v + ac * w;
    return {std::sqrt(length2(point - p)), p, faceNormal};
}

struct Aabb {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};
    void expand(Vec3 p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }
    [[nodiscard]] float distance2(Vec3 p) const {
        const float dx = p.x < min.x ? min.x - p.x : (p.x > max.x ? p.x - max.x : 0.f);
        const float dy = p.y < min.y ? min.y - p.y : (p.y > max.y ? p.y - max.y : 0.f);
        const float dz = p.z < min.z ? min.z - p.z : (p.z > max.z ? p.z - max.z : 0.f);
        return dx * dx + dy * dy + dz * dz;
    }
};

struct TriangleRef {
    std::uint32_t i0 = 0, i1 = 0, i2 = 0;
    Vec3          centroid{};
    Aabb          bounds{};
};

struct BvhNode {
    Aabb         bounds{};
    std::int32_t left  = -1;
    std::int32_t right = -1;
    std::int32_t begin = 0;
    std::int32_t count = 0;
};

struct SurfaceBvh {
    const MeshBuild*          mesh = nullptr;
    std::vector<TriangleRef>  triangles;
    std::vector<std::int32_t> order;
    std::vector<BvhNode>      nodes;

    void build(const MeshBuild& source, const std::vector<std::int32_t>& vertexSourceIds, std::int32_t sourceId) {
        mesh = &source;
        triangles.clear();
        order.clear();
        nodes.clear();
        for (int t = 0; t + 2 < source.getIndexCount(); t += 3) {
            const auto i0 = static_cast<std::uint32_t>(source.getIndex(t));
            const auto i1 = static_cast<std::uint32_t>(source.getIndex(t + 1));
            const auto i2 = static_cast<std::uint32_t>(source.getIndex(t + 2));
            if (static_cast<std::size_t>(i0) >= vertexSourceIds.size()) continue;
            if (vertexSourceIds[i0] != sourceId) continue;
            TriangleRef tri;
            tri.i0 = i0;
            tri.i1 = i1;
            tri.i2 = i2;
            const Vec3 a{source.getPositionX(static_cast<int>(i0)), source.getPositionY(static_cast<int>(i0)),
                         source.getPositionZ(static_cast<int>(i0))};
            const Vec3 b{source.getPositionX(static_cast<int>(i1)), source.getPositionY(static_cast<int>(i1)),
                         source.getPositionZ(static_cast<int>(i1))};
            const Vec3 c{source.getPositionX(static_cast<int>(i2)), source.getPositionY(static_cast<int>(i2)),
                         source.getPositionZ(static_cast<int>(i2))};
            tri.centroid = (a + b + c) * (1.f / 3.f);
            tri.bounds.expand(a);
            tri.bounds.expand(b);
            tri.bounds.expand(c);
            triangles.push_back(tri);
        }
        if (triangles.empty()) return;
        order.resize(triangles.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<std::int32_t>(i);
        nodes.reserve(triangles.size() * 2u);
        buildNode(0, static_cast<std::int32_t>(order.size()), 0);
    }

    std::int32_t buildNode(std::int32_t begin, std::int32_t end, int depth) {
        BvhNode node;
        node.begin = begin;
        node.count = end - begin;
        for (std::int32_t i = begin; i < end; ++i)
            node.bounds.expand(triangles[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])].bounds);
        const auto index = static_cast<std::int32_t>(nodes.size());
        nodes.push_back(node);
        if (node.count <= 8) return index;
        const int  axis = depth % 3;
        const auto mid  = begin + node.count / 2;
        std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
                         [&](std::int32_t a, std::int32_t b) {
                             const auto& ca = triangles[static_cast<std::size_t>(a)].centroid;
                             const auto& cb = triangles[static_cast<std::size_t>(b)].centroid;
                             if (axis == 0) return ca.x < cb.x || (ca.x == cb.x && a < b);
                             if (axis == 1) return ca.y < cb.y || (ca.y == cb.y && a < b);
                             return ca.z < cb.z || (ca.z == cb.z && a < b);
                         });
        nodes[static_cast<std::size_t>(index)].left  = buildNode(begin, mid, depth + 1);
        nodes[static_cast<std::size_t>(index)].right = buildNode(mid, end, depth + 1);
        nodes[static_cast<std::size_t>(index)].count = 0;
        return index;
    }

    void queryNode(std::int32_t nodeIndex, Vec3 point, float maxDistance, ClosestHit& best) const {
        if (nodeIndex < 0 || mesh == nullptr || nodes.empty()) return;
        const auto& node = nodes[static_cast<std::size_t>(nodeIndex)];
        if (node.bounds.distance2(point) > maxDistance * maxDistance) return;
        if (node.count > 0) {
            for (std::int32_t i = 0; i < node.count; ++i) {
                const auto& tri =
                    triangles[static_cast<std::size_t>(order[static_cast<std::size_t>(node.begin + i)])];
                const Vec3 a{mesh->getPositionX(static_cast<int>(tri.i0)), mesh->getPositionY(static_cast<int>(tri.i0)),
                             mesh->getPositionZ(static_cast<int>(tri.i0))};
                const Vec3 b{mesh->getPositionX(static_cast<int>(tri.i1)), mesh->getPositionY(static_cast<int>(tri.i1)),
                             mesh->getPositionZ(static_cast<int>(tri.i1))};
                const Vec3 c{mesh->getPositionX(static_cast<int>(tri.i2)), mesh->getPositionY(static_cast<int>(tri.i2)),
                             mesh->getPositionZ(static_cast<int>(tri.i2))};
                auto hit = closestOnTriangle(point, a, b, c);
                if (hit.distance < best.distance && hit.distance <= maxDistance) best = hit;
            }
            return;
        }
        queryNode(node.left, point, maxDistance, best);
        queryNode(node.right, point, maxDistance, best);
    }

    [[nodiscard]] ClosestHit closest(Vec3 point, float maxDistance) const {
        ClosestHit best;
        if (!nodes.empty()) queryNode(0, point, maxDistance, best);
        return best;
    }
};

Result<void> validateParams(const MeshContactBlendParams& params) {
    if (!std::isfinite(params.edgeRadius) || params.edgeRadius < 0.f || !std::isfinite(params.materialRadius) ||
        params.materialRadius < 0.f || !std::isfinite(params.strength) || params.strength < 0.f ||
        !std::isfinite(params.normalsBlend) || params.normalsBlend < 0.f || !std::isfinite(params.materialBlend) ||
        params.materialBlend < 0.f || !std::isfinite(params.maxQueryDistance) || params.maxQueryDistance < 0.f ||
        !std::isfinite(params.surfaceOffset))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "contact blend parameters must be finite and non-negative",
                                                       "params", {}, "procgen.mesh.blend"));
    if (params.falloff != "smooth" && params.falloff != "linear" && params.falloff != "sharp" &&
        params.falloff != "sphere")
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "contact blend falloff must be smooth|linear|sharp|sphere",
                                                       "falloff", {}, "procgen.mesh.blend"));
    return Result<void>::success();
}

void applyHitToVertex(MeshBuild& output, std::vector<float>& colors, int vertex, const ClosestHit& best,
                      float maxQuery, const MeshContactBlendParams& params) {
    auto&             positions = output.positions();
    auto&             normals   = output.normals();
    const std::size_t base      = static_cast<std::size_t>(vertex) * 3u;
    const Vec3        origin{positions[base], positions[base + 1u], positions[base + 2u]};
    if (!std::isfinite(best.distance) || best.distance > maxQuery) {
        colors[static_cast<std::size_t>(vertex) * 4u + 3u] = 0.f;
        return;
    }
    float edgeWeight = 0.f;
    if (params.edgeRadius > 0.f && params.strength > 0.f) {
        const float t = 1.f - saturate(best.distance / params.edgeRadius);
        edgeWeight    = saturate(evaluateFalloff(params.falloff, t) * params.strength);
    }
    float materialWeight = 0.f;
    if (params.materialRadius > 0.f && params.materialBlend > 0.f) {
        const float t  = 1.f - saturate(best.distance / params.materialRadius);
        materialWeight = saturate(evaluateFalloff(params.falloff, t) * params.materialBlend);
    }
    if (params.softSnapPositions && edgeWeight > 0.f) {
        const Vec3 target  = best.point + best.normal * params.surfaceOffset;
        const Vec3 blended = origin + (target - origin) * edgeWeight;
        positions[base]     = blended.x;
        positions[base + 1] = blended.y;
        positions[base + 2] = blended.z;
    }
    if (params.normalsBlend > 0.f && edgeWeight > 0.f) {
        const Vec3  n0{normals[base], normals[base + 1u], normals[base + 2u]};
        const float w  = saturate(edgeWeight * params.normalsBlend);
        const Vec3  n1 = normalized(n0 * (1.f - w) + best.normal * w);
        normals[base]     = n1.x;
        normals[base + 1] = n1.y;
        normals[base + 2] = n1.z;
    }
    colors[static_cast<std::size_t>(vertex) * 4u + 3u] = materialWeight;
}

Result<MeshBuild> blendMultiImpl(const MeshBuild& mesh, const std::vector<std::int32_t>& vertexSourceIds,
                                  const MeshContactBlendParams& params) {
    const int vertexCount = mesh.getVertexCount();
    if (vertexCount < 0 || static_cast<std::size_t>(vertexCount) != vertexSourceIds.size())
        return Result<MeshBuild>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "contact blend vertexSourceIds size must match vertex count",
            "vertexSourceIds", {}, "procgen.mesh.blend"));
    auto validated = validateParams(params);
    if (!validated.ok()) return Result<MeshBuild>::failure(validated.status());

    float maxQuery = params.maxQueryDistance;
    if (maxQuery <= 0.f) maxQuery = std::max(params.edgeRadius, params.materialRadius);
    if (maxQuery <= 0.f) {
        MeshBuild copy = mesh;
        copy.setMeta("contactBlend", "identity");
        return Result<MeshBuild>::success(std::move(copy));
    }

    std::vector<std::int32_t> uniqueSources = vertexSourceIds;
    std::sort(uniqueSources.begin(), uniqueSources.end());
    uniqueSources.erase(std::unique(uniqueSources.begin(), uniqueSources.end()), uniqueSources.end());
    if (uniqueSources.size() < 2)
        return Result<MeshBuild>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "contact blend requires at least two distinct source ids",
            "vertexSourceIds", {}, "procgen.mesh.blend"));

    std::vector<SurfaceBvh> bvhs(uniqueSources.size());
    for (std::size_t i = 0; i < uniqueSources.size(); ++i)
        bvhs[i].build(mesh, vertexSourceIds, uniqueSources[i]);

    MeshBuild          output = mesh;
    std::vector<float> colors =
        mesh.hasVertexColors() ? mesh.colors()
                               : std::vector<float>(static_cast<std::size_t>(vertexCount) * 4u, 1.f);
    auto& positions = output.positions();
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const auto        sourceId = vertexSourceIds[static_cast<std::size_t>(vertex)];
        const std::size_t base     = static_cast<std::size_t>(vertex) * 3u;
        const Vec3        origin{positions[base], positions[base + 1u], positions[base + 2u]};
        ClosestHit        best;
        for (std::size_t i = 0; i < uniqueSources.size(); ++i) {
            if (uniqueSources[i] == sourceId) continue;
            auto hit = bvhs[i].closest(origin, maxQuery);
            if (hit.distance < best.distance) best = hit;
        }
        applyHitToVertex(output, colors, vertex, best, maxQuery, params);
    }

    auto setColors = output.setVertexColors(std::move(colors));
    if (!setColors.ok()) return Result<MeshBuild>::failure(setColors.status());
    output.setMeta("contactBlend", "applied");
    output.setMeta("contactBlend.falloff", std::string(params.falloff));
    return Result<MeshBuild>::success(std::move(output));
}

Result<MeshBuild> blendAgainstImpl(const MeshBuild& movable, const MeshBuild& surface,
                                   const MeshContactBlendParams& params) {
    if (movable.empty())
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "contact blend movable mesh is empty", "movable", {},
                                                            "procgen.mesh.blend"));
    if (surface.empty())
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "contact blend surface mesh is empty", "surface", {},
                                                            "procgen.mesh.blend"));
    auto validated = validateParams(params);
    if (!validated.ok()) return Result<MeshBuild>::failure(validated.status());

    float maxQuery = params.maxQueryDistance;
    if (maxQuery <= 0.f) maxQuery = std::max(params.edgeRadius, params.materialRadius);
    if (maxQuery <= 0.f) {
        MeshBuild copy = movable;
        copy.setMeta("contactBlend", "identity");
        return Result<MeshBuild>::success(std::move(copy));
    }

    // Surface-only BVH: every surface vertex tagged source 1; query ignores movable topology.
    std::vector<std::int32_t> surfaceIds(static_cast<std::size_t>(surface.getVertexCount()), 1);
    SurfaceBvh                bvh;
    bvh.build(surface, surfaceIds, 1);
    if (bvh.triangles.empty())
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "contact blend surface has no triangles", "surface", {},
                                                            "procgen.mesh.blend"));

    MeshBuild          output = movable;
    const int          vertexCount = output.getVertexCount();
    std::vector<float> colors =
        movable.hasVertexColors() ? movable.colors()
                                  : std::vector<float>(static_cast<std::size_t>(vertexCount) * 4u, 1.f);
    auto& positions = output.positions();
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const std::size_t base = static_cast<std::size_t>(vertex) * 3u;
        const Vec3 origin{positions[base], positions[base + 1u], positions[base + 2u]};
        applyHitToVertex(output, colors, vertex, bvh.closest(origin, maxQuery), maxQuery, params);
    }
    auto setColors = output.setVertexColors(std::move(colors));
    if (!setColors.ok()) return Result<MeshBuild>::failure(setColors.status());
    output.setMeta("contactBlend", "againstSurface");
    output.setMeta("contactBlend.falloff", std::string(params.falloff));
    return Result<MeshBuild>::success(std::move(output));
}

}  // namespace

Result<MeshBuild> meshContactBlendResult(const MeshBuild& mesh, const std::vector<std::int32_t>& vertexSourceIds,
                                         const MeshContactBlendParams& params) {
    return blendMultiImpl(mesh, vertexSourceIds, params);
}

Result<MeshBuild> meshContactBlendAgainstSurfaceResult(const MeshBuild& movable, const MeshBuild& surface,
                                                       const MeshContactBlendParams& params) {
    return blendAgainstImpl(movable, surface, params);
}

}  // namespace eve::procgen
