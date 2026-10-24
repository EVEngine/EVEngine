#include "procgen/road/RoadAuthoringInternal.h"
#include "procgen/road/RoadBakeInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace eve::procgen::road::detail {
namespace {
Result<void> invalidPrefab(std::string message) {
    return bakeFail<void>(DiagnosticCode::InvalidArgument, message, "facility.prefab");
}
RoadPrefabVertex mix(const RoadPrefabVertex& a, const RoadPrefabVertex& b, float t) {
    return {std::lerp(a.x, b.x, t),   std::lerp(a.y, b.y, t),   std::lerp(a.z, b.z, t), std::lerp(a.nx, b.nx, t),
            std::lerp(a.ny, b.ny, t), std::lerp(a.nz, b.nz, t), std::lerp(a.u, b.u, t), std::lerp(a.v, b.v, t)};
}
std::vector<RoadPrefabVertex> clip(const std::vector<RoadPrefabVertex>& polygon, float plane, bool above) {
    std::vector<RoadPrefabVertex> out;
    if (polygon.empty()) return out;
    auto previous  = polygon.back();
    bool wasInside = above ? previous.z >= plane : previous.z <= plane;
    for (const auto& current : polygon) {
        const bool inside = above ? current.z >= plane : current.z <= plane;
        if (inside != wasInside) out.push_back(mix(previous, current, (plane - previous.z) / (current.z - previous.z)));
        if (inside) out.push_back(current);
        previous  = current;
        wasInside = inside;
    }
    return out;
}
V3 position(const SplineFrameSample& a, const SplineFrameSample& b, const RoadPrefabVertex& v, float t) {
    const V3 side =
        normalize(V3{std::lerp(a.sideX, b.sideX, t), std::lerp(a.sideY, b.sideY, t), std::lerp(a.sideZ, b.sideZ, t)});
    const V3 up = normalize(V3{std::lerp(a.upX, b.upX, t), std::lerp(a.upY, b.upY, t), std::lerp(a.upZ, b.upZ, t)});
    return V3{std::lerp(a.sample.x, b.sample.x, t), std::lerp(a.sample.y, b.sample.y, t),
              std::lerp(a.sample.z, b.sample.z, t)} +
           side * v.x + up * v.y;
}
}  // namespace
Result<void> validateFacilityPrefab(const RoadFacilityPrefab& p) {
    if (!std::isfinite(p.repeatSourceLength) || p.repeatSourceLength < 0.f || p.repeatSourceLength > 10000000.f)
        return invalidPrefab("invalid prefab repetition span");
    if (!std::isfinite(p.length) || p.length <= 1e-4f || p.length > 10000.f || !std::isfinite(p.sourceStart) ||
        !std::isfinite(p.sourceEnd) || p.sourceStart < 0.f || p.sourceEnd > 1.f || p.sourceStart >= p.sourceEnd)
        return invalidPrefab("invalid prefab source mapping");
    if (p.vertices.empty()) {
        if (!p.triangles.empty() || !p.assetId.empty() || !p.startMesh.vertices.empty() ||
            !p.endMesh.vertices.empty() || !p.startMesh.triangles.empty() || !p.endMesh.triangles.empty() ||
            !p.startMesh.assetId.empty() || !p.endMesh.assetId.empty() || p.middleCopies != 1 ||
            p.weldCompatibleVertices || p.automaticSpacing || !p.averageSpacing || p.reverseRepeatSequence ||
            p.repeatSourceLength != 0.f)
            return invalidPrefab("empty prefab carries mesh or identity");
        return Result<void>::success();
    }
    if (p.middleCopies < 1 || p.middleCopies > 4096 || p.assetId.empty() || p.assetId.size() > 256u ||
        p.vertices.size() > 65536u || p.triangles.empty() || p.triangles.size() > 131072u || !std::isfinite(p.length) ||
        p.length <= 1e-4f || p.length > 10000.f || !std::isfinite(p.sourceStart) || !std::isfinite(p.sourceEnd) ||
        p.sourceStart < 0.f || p.sourceEnd > 1.f || p.sourceEnd - p.sourceStart <= 1e-5f)
        return invalidPrefab("invalid prefab identity, budget or source interval");
    for (const auto& v : p.vertices) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || std::fabs(v.x) > 10000.f ||
            std::fabs(v.y) > 10000.f || v.z < 0.f || v.z > p.length || !std::isfinite(v.nx) || !std::isfinite(v.ny) ||
            !std::isfinite(v.nz) || std::fabs(length(V3{v.nx, v.ny, v.nz}) - 1.f) > 1e-3f || !std::isfinite(v.u) ||
            !std::isfinite(v.v))
            return invalidPrefab("invalid prefab vertex");
    }
    for (const auto& t : p.triangles) {
        if (t.a >= p.vertices.size() || t.b >= p.vertices.size() || t.c >= p.vertices.size() ||
            t.material > RoadMaterial::TunnelPortalAsphalt || t.smoothingGroup > 65535u)
            return invalidPrefab("invalid prefab triangle");
        const auto &a = p.vertices[t.a], &b = p.vertices[t.b], &c = p.vertices[t.c];
        const V3    normal = cross(V3{b.x - a.x, b.y - a.y, b.z - a.z}, V3{c.x - a.x, c.y - a.y, c.z - a.z});
        if (length(normal) < 1e-7f ||
            dot(normal, V3{a.nx + b.nx + c.nx, a.ny + b.ny + c.ny, a.nz + b.nz + c.nz}) <= 0.f)
            return invalidPrefab("degenerate or reversed prefab triangle");
    }
    for (const auto* part : {&p.startMesh, &p.endMesh}) {
        RoadFacilityPrefab source;
        source.assetId   = part->assetId;
        source.length    = part->length;
        source.vertices  = part->vertices;
        source.triangles = part->triangles;
        // Default empty segments stop recursion; only nonempty segment meshes need validation.
        if (!part->vertices.empty() || !part->triangles.empty() || !part->assetId.empty()) {
            auto valid = validateFacilityPrefab(source);
            if (!valid.ok()) return valid;
        }
    }
    return Result<void>::success();
}
Result<void> bakeFacilityPrefab(MeshBuild& mesh, const RoadFacilityPrefab& prefab,
                                const std::vector<SplineFrameSample>& frames, float sourceLow, float sourceHigh,
                                float fullSourceLength, std::size_t budget) {
    auto valid = validateFacilityPrefab(prefab);
    if (!valid.ok()) return valid;
    if (frames.size() < 2 || sourceHigh - sourceLow <= 1e-5f) return invalidPrefab("degenerate deformation interval");
    int   copies       = prefab.middleCopies;
    float middleLength = prefab.length, remainder = prefab.length;
    if (prefab.automaticSpacing) {
        const float span = prefab.repeatSourceLength > 0.f ? prefab.repeatSourceLength : fullSourceLength;
        const float caps = (prefab.startMesh.vertices.empty() ? 0.f : prefab.startMesh.length) +
                           (prefab.endMesh.vertices.empty() ? 0.f : prefab.endMesh.length);
        if (!std::isfinite(span) || span <= caps + 1e-5f)
            return invalidPrefab("automatic spacing requires arc room for start, middle and end");
        const double ratio = static_cast<double>(span - caps) / prefab.length;
        const double count = std::max(1.0, prefab.averageSpacing ? std::round(ratio) : std::ceil(ratio));
        if (count > 4096.0) return invalidPrefab("automatic spacing exceeds 4096 middle segments");
        copies = static_cast<int>(count);
        if (prefab.averageSpacing) middleLength = (span - caps) / static_cast<float>(copies);
        remainder = (span - caps) - static_cast<float>(copies - 1) * middleLength;
    }
    const auto triangleCount = prefab.triangles.size() * static_cast<std::size_t>(copies) +
                               prefab.startMesh.triangles.size() + prefab.endMesh.triangles.size();
    if (triangleCount > (budget / 12u) / (frames.size() - 1u))
        return bakeFail<void>(DiagnosticCode::PreconditionViolation, "prefab tessellation exceeds work budget",
                              "prefab");
    const float begin = frames.front().sample.normalizedDistance, end = frames.back().sample.normalizedDistance;
    struct Part {
        const std::vector<RoadPrefabVertex>*   vertices;
        const std::vector<RoadPrefabTriangle>* triangles;
        float                                  offset, length, sourceLength, sourceOffset;
    };
    std::vector<Part> parts;
    float             total = 0.f;
    if (!prefab.startMesh.vertices.empty()) {
        parts.push_back({&prefab.startMesh.vertices, &prefab.startMesh.triangles, total, prefab.startMesh.length,
                         prefab.startMesh.length, 0.f});
        total += prefab.startMesh.length;
    }
    for (int i = 0; i < copies; ++i) {
        const bool partial = prefab.automaticSpacing && !prefab.averageSpacing &&
                             (prefab.reverseRepeatSequence ? i == 0 : i == copies - 1);
        const float span = partial ? remainder : middleLength;
        parts.push_back({&prefab.vertices, &prefab.triangles, total, span,
                         prefab.automaticSpacing && prefab.averageSpacing ? prefab.length : span,
                         partial && prefab.reverseRepeatSequence ? prefab.length - remainder : 0.f});
        total += span;
    }
    if (!prefab.endMesh.vertices.empty()) {
        parts.push_back({&prefab.endMesh.vertices, &prefab.endMesh.triangles, total, prefab.endMesh.length,
                         prefab.endMesh.length, 0.f});
        total += prefab.endMesh.length;
    }
    using SmoothKey = std::array<float, 4>;
    struct Smooth {
        V3                         sum{};
        std::vector<std::uint32_t> vertices;
    };
    std::map<SmoothKey, Smooth>                   smoothing;
    std::map<std::array<float, 9>, std::uint32_t> welded;
    for (std::size_t slab = 1; slab < frames.size(); ++slab) {
        const auto &a = frames[slab - 1], &b = frames[slab];
        if (length(V3{b.sample.x - a.sample.x, b.sample.y - a.sample.y, b.sample.z - a.sample.z}) <= 1e-5f) continue;
        const float low =
            std::lerp(sourceLow, sourceHigh, (a.sample.normalizedDistance - begin) / (end - begin)) * total;
        const float high =
            std::lerp(sourceLow, sourceHigh, (b.sample.normalizedDistance - begin) / (end - begin)) * total;
        if (high - low <= 1e-6f) continue;
        for (const auto& part : parts) {
            if (part.offset + part.length < low || part.offset > high) continue;
            for (const auto& triangle : *part.triangles) {
                auto mapped = [&](std::uint32_t index) {
                    auto        v     = (*part.vertices)[index];
                    const float scale = part.length / part.sourceLength;
                    v.z               = (v.z - part.sourceOffset) * scale;
                    const auto normal = normalize(V3{v.nx, v.ny, v.nz / scale});
                    v.nx              = normal.x;
                    v.ny              = normal.y;
                    v.nz              = normal.z;
                    return v;
                };
                auto polygon = clip(clip({mapped(triangle.a), mapped(triangle.b), mapped(triangle.c)},
                                         std::max(0.f, low - part.offset), true),
                                    std::min(part.length, high - part.offset), false);
                if (polygon.size() < 3) continue;
                for (std::size_t i = 2; i < polygon.size(); ++i) {
                    const RoadPrefabVertex vertices[]{polygon[0], polygon[i - 1], polygon[i]};
                    if (static_cast<std::size_t>(mesh.getVertexCount()) +
                            static_cast<std::size_t>(mesh.getIndexCount()) + 6u >
                        budget)
                        return bakeFail<void>(DiagnosticCode::PreconditionViolation, "prefab exceeds output budget",
                                              "mesh");
                    V3 positions[3], normals[3];
                    for (int k = 0; k < 3; ++k) {
                        const auto& v = vertices[k];
                        const float t = std::clamp((v.z + part.offset - low) / (high - low), 0.f, 1.f);
                        positions[k]  = position(a, b, v, t);
                        const V3 side = normalize(V3{std::lerp(a.sideX, b.sideX, t), std::lerp(a.sideY, b.sideY, t),
                                                     std::lerp(a.sideZ, b.sideZ, t)});
                        const V3 up   = normalize(
                            V3{std::lerp(a.upX, b.upX, t), std::lerp(a.upY, b.upY, t), std::lerp(a.upZ, b.upZ, t)});
                        const float t0 = std::max(0.f, t - .001f), t1 = std::min(1.f, t + .001f);
                        const V3    longitudinal =
                            (position(a, b, v, t1) - position(a, b, v, t0)) * (1.f / ((t1 - t0) * (high - low)));
                        const float determinant = dot(side, cross(up, longitudinal));
                        if (!std::isfinite(determinant) || determinant <= 1e-6f)
                            return invalidPrefab("prefab deformation folds or collapses; slab=" + std::to_string(slab) +
                                                 " determinant=" + std::to_string(determinant) +
                                                 " t=" + std::to_string(t) + " x0=" + std::to_string(a.sample.x) +
                                                 " x1=" + std::to_string(b.sample.x));
                        normals[k] = normalize((cross(up, longitudinal) * v.nx + cross(longitudinal, side) * v.ny +
                                                cross(side, up) * v.nz) *
                                               (1.f / determinant));
                    }
                    const V3 face = cross(positions[1] - positions[0], positions[2] - positions[0]);
                    if (length(face) < 1e-7f) continue;
                    mesh.setActiveGroup(roadMaterialGroup(triangle.material));
                    std::uint32_t indices[3]{};
                    for (int k = 0; k < 3; ++k) {
                        std::array<float, 9> key{
                            positions[k].x, positions[k].y, positions[k].z,
                            normals[k].x,   normals[k].y,   normals[k].z,
                            vertices[k].u,  vertices[k].v,  static_cast<float>(triangle.smoothingGroup)};
                        for (float& v : key)
                            v = static_cast<float>(std::round(static_cast<double>(v) * 100000.0) / 100000.0);
                        const auto found = welded.find(key);
                        if (triangle.smoothingGroup != 0u) {
                            auto& smooth = smoothing[SmoothKey{key[0], key[1], key[2], key[8]}];
                            smooth.sum   = smooth.sum + face;
                            smooth.vertices.push_back(prefab.weldCompatibleVertices && found != welded.end()
                                                          ? found->second
                                                          : static_cast<std::uint32_t>(mesh.getVertexCount()));
                        }
                        if (prefab.weldCompatibleVertices && found != welded.end()) {
                            indices[k] = found->second;
                            continue;
                        }
                        indices[k] = static_cast<std::uint32_t>(mesh.getVertexCount());
                        mesh.addVertex(positions[k].x, positions[k].y, positions[k].z, normals[k].x, normals[k].y,
                                       normals[k].z, vertices[k].u, vertices[k].v);
                        if (prefab.weldCompatibleVertices) welded.emplace(key, indices[k]);
                    }
                    mesh.addTriangle(indices[0], indices[1], indices[2]);
                }
            }
        }
    }
    for (const auto& [key, smooth] : smoothing) {
        (void)key;
        if (length(smooth.sum) <= 1e-7f) return invalidPrefab("smoothing group has cancelling face normals");
        const auto normal = normalize(smooth.sum);
        for (auto index : smooth.vertices) {
            mesh.normals()[index * 3u]      = normal.x;
            mesh.normals()[index * 3u + 1u] = normal.y;
            mesh.normals()[index * 3u + 2u] = normal.z;
        }
    }
    return Result<void>::success();
}
}  // namespace eve::procgen::road::detail
