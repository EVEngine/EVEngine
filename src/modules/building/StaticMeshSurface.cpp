#include "building/StaticMeshSurface.h"

#include "building/PlacementWorld.h"
#include "grid/GridConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace eve::building {
namespace {

constexpr float kEpsilon = 1e-6f;

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 subtract(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float lengthSquared(const Vec3 &value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

template <class T>
eve::Result<T> meshFailure(eve::DiagnosticCode code, const std::string &message,
                           const std::string &subject = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, message, subject, {}, "building.static-mesh-surface"));
}

}  // namespace

StaticMeshSurface::StaticMeshSurface(Config config, std::vector<float> vertices,
                                     std::vector<uint32_t> indices,
                                     std::vector<float> normals)
    : config_(std::move(config)),
      vertices_(std::move(vertices)),
      indices_(std::move(indices)),
      normals_(std::move(normals)) {
    const uint32_t triangleCount = static_cast<uint32_t>(indices_.size() / 3);
    triangleOrder_.resize(triangleCount);
    std::iota(triangleOrder_.begin(), triangleOrder_.end(), uint32_t{0});
    triangleBounds_.reserve(triangleCount);
    for (uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
        Bounds bounds{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const size_t vertex = static_cast<size_t>(indices_[triangle * 3 + corner]) * 3;
            bounds.minX = std::min(bounds.minX, vertices_[vertex]);
            bounds.minY = std::min(bounds.minY, vertices_[vertex + 1]);
            bounds.minZ = std::min(bounds.minZ, vertices_[vertex + 2]);
            bounds.maxX = std::max(bounds.maxX, vertices_[vertex]);
            bounds.maxY = std::max(bounds.maxY, vertices_[vertex + 1]);
            bounds.maxZ = std::max(bounds.maxZ, vertices_[vertex + 2]);
        }
        triangleBounds_.push_back(bounds);
    }
    nodes_.reserve(triangleCount * 2);
    buildNode(0, triangleCount);
}

eve::Result<std::shared_ptr<const StaticMeshSurface>> StaticMeshSurface::create(
    Config config, std::vector<float> vertices, std::vector<uint32_t> indices,
    std::vector<float> normals) {
    if (vertices.size() < 9 || vertices.size() % 3 != 0 || indices.empty() ||
        indices.size() % 3 != 0) {
        return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "static mesh requires packed XYZ vertices and complete indexed triangles",
            config.surfaceId);
    }
    if (indices.size() / 3 > std::numeric_limits<uint32_t>::max()) {
        return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
            eve::DiagnosticCode::InvalidArgument, "static mesh has too many triangles",
            config.surfaceId);
    }
    if (!normals.empty() && normals.size() != vertices.size()) {
        return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "static mesh normals must be empty or match packed vertices", config.surfaceId);
    }
    if (!std::isfinite(config.referenceHeight) || config.leafTriangleCount == 0 ||
        config.leafTriangleCount > 64) {
        return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
            eve::DiagnosticCode::InvalidArgument,
            "static mesh requires a finite reference height and leaf size in [1, 64]",
            config.surfaceId);
    }
    for (float value : vertices) {
        if (!std::isfinite(value))
            return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
                eve::DiagnosticCode::InvalidArgument, "static mesh vertices must be finite",
                config.surfaceId);
    }
    for (float value : normals) {
        if (!std::isfinite(value))
            return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
                eve::DiagnosticCode::InvalidArgument, "static mesh normals must be finite",
                config.surfaceId);
    }
    const size_t vertexCount = vertices.size() / 3;
    for (size_t triangle = 0; triangle < indices.size() / 3; ++triangle) {
        std::array<Vec3, 3> points;
        for (size_t corner = 0; corner < 3; ++corner) {
            const uint32_t index = indices[triangle * 3 + corner];
            if (index >= vertexCount)
                return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
                    eve::DiagnosticCode::InvalidArgument,
                    "static mesh contains an out-of-range vertex index", config.surfaceId);
            const size_t offset = static_cast<size_t>(index) * 3;
            points[corner] = {vertices[offset], vertices[offset + 1], vertices[offset + 2]};
        }
        if (lengthSquared(cross(subtract(points[1], points[0]),
                                subtract(points[2], points[0]))) <= kEpsilon * kEpsilon) {
            return meshFailure<std::shared_ptr<const StaticMeshSurface>>(
                eve::DiagnosticCode::InvalidArgument,
                "static mesh contains a degenerate triangle", config.surfaceId);
        }
    }
    return eve::Result<std::shared_ptr<const StaticMeshSurface>>::success(
        std::shared_ptr<const StaticMeshSurface>(new StaticMeshSurface(
            std::move(config), std::move(vertices), std::move(indices), std::move(normals))));
}

uint32_t StaticMeshSurface::buildNode(uint32_t first, uint32_t count) {
    Bounds bounds{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    Bounds centroids = bounds;
    for (uint32_t i = first; i < first + count; ++i) {
        const Bounds &triangle = triangleBounds_[triangleOrder_[i]];
        bounds.minX = std::min(bounds.minX, triangle.minX);
        bounds.minY = std::min(bounds.minY, triangle.minY);
        bounds.minZ = std::min(bounds.minZ, triangle.minZ);
        bounds.maxX = std::max(bounds.maxX, triangle.maxX);
        bounds.maxY = std::max(bounds.maxY, triangle.maxY);
        bounds.maxZ = std::max(bounds.maxZ, triangle.maxZ);
        const float cx = triangle.minX + triangle.maxX;
        const float cy = triangle.minY + triangle.maxY;
        const float cz = triangle.minZ + triangle.maxZ;
        centroids.minX = std::min(centroids.minX, cx);
        centroids.minY = std::min(centroids.minY, cy);
        centroids.minZ = std::min(centroids.minZ, cz);
        centroids.maxX = std::max(centroids.maxX, cx);
        centroids.maxY = std::max(centroids.maxY, cy);
        centroids.maxZ = std::max(centroids.maxZ, cz);
    }
    const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({bounds, first, count, 0, 0, true});
    if (count <= config_.leafTriangleCount) return nodeIndex;

    const std::array<float, 3> extents{centroids.maxX - centroids.minX,
                                       centroids.maxY - centroids.minY,
                                       centroids.maxZ - centroids.minZ};
    const int axis = static_cast<int>(
        std::distance(extents.begin(), std::max_element(extents.begin(), extents.end())));
    const auto center = [&](uint32_t triangle) {
        const Bounds &value = triangleBounds_[triangle];
        if (axis == 0) return value.minX + value.maxX;
        if (axis == 1) return value.minY + value.maxY;
        return value.minZ + value.maxZ;
    };
    const uint32_t middle = first + count / 2;
    std::nth_element(triangleOrder_.begin() + first, triangleOrder_.begin() + middle,
                     triangleOrder_.begin() + first + count,
                     [&](uint32_t a, uint32_t b) {
                         const float ac = center(a);
                         const float bc = center(b);
                         return ac == bc ? a < b : ac < bc;
                     });
    const uint32_t left = buildNode(first, middle - first);
    const uint32_t right = buildNode(middle, first + count - middle);
    nodes_[nodeIndex].leaf = false;
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = right;
    nodes_[nodeIndex].count = 0;
    return nodeIndex;
}

eve::Result<PlacementSystem::PlacementHit> StaticMeshSurface::sample(
    const PlacementWorld &world, float planeX, float planeY) const {
    if (!std::isfinite(planeX) || !std::isfinite(planeY))
        return meshFailure<PlacementSystem::PlacementHit>(
            eve::DiagnosticCode::InvalidArgument, "mesh sample coordinates must be finite",
            config_.surfaceId);

    const bool xz = world.getGrid().plane == grid::GridPlane::XZ;
    bool found = false;
    float bestHeight = 0.f;
    float bestDistance = 0.f;
    uint32_t bestTriangle = 0;
    float bestA = 0.f;
    float bestB = 0.f;
    float bestC = 0.f;
    std::vector<uint32_t> pending{0};
    while (!pending.empty()) {
        const Node &node = nodes_[pending.back()];
        pending.pop_back();
        const bool inside = planeX >= node.bounds.minX - kEpsilon &&
                            planeX <= node.bounds.maxX + kEpsilon &&
                            planeY >= (xz ? node.bounds.minZ : node.bounds.minY) - kEpsilon &&
                            planeY <= (xz ? node.bounds.maxZ : node.bounds.maxY) + kEpsilon;
        if (!inside) continue;
        if (!node.leaf) {
            pending.push_back(node.right);
            pending.push_back(node.left);
            continue;
        }
        for (uint32_t ordered = node.first; ordered < node.first + node.count; ++ordered) {
            const uint32_t triangle = triangleOrder_[ordered];
            std::array<Vec3, 3> p;
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const size_t offset = static_cast<size_t>(indices_[triangle * 3 + corner]) * 3;
                p[corner] = {vertices_[offset], vertices_[offset + 1], vertices_[offset + 2]};
            }
            const float p0x = p[0].x;
            const float p0y = xz ? p[0].z : p[0].y;
            const float p1x = p[1].x;
            const float p1y = xz ? p[1].z : p[1].y;
            const float p2x = p[2].x;
            const float p2y = xz ? p[2].z : p[2].y;
            const float denominator =
                (p1y - p2y) * (p0x - p2x) + (p2x - p1x) * (p0y - p2y);
            if (std::fabs(denominator) <= kEpsilon) continue;
            const float a = ((p1y - p2y) * (planeX - p2x) +
                             (p2x - p1x) * (planeY - p2y)) /
                            denominator;
            const float b = ((p2y - p0y) * (planeX - p2x) +
                             (p0x - p2x) * (planeY - p2y)) /
                            denominator;
            const float c = 1.f - a - b;
            if (a < -kEpsilon || b < -kEpsilon || c < -kEpsilon) continue;
            const float height = xz ? a * p[0].y + b * p[1].y + c * p[2].y
                                    : a * p[0].z + b * p[1].z + c * p[2].z;
            const float distance = std::fabs(height - config_.referenceHeight);
            bool better = !found;
            if (found && config_.hitSelection == HitSelection::Highest)
                better = height > bestHeight + kEpsilon;
            else if (found && config_.hitSelection == HitSelection::Lowest)
                better = height < bestHeight - kEpsilon;
            else if (found && config_.hitSelection == HitSelection::ClosestToReference)
                better = distance < bestDistance - kEpsilon;
            const bool tied = config_.hitSelection == HitSelection::ClosestToReference
                                  ? std::fabs(distance - bestDistance) <= kEpsilon
                                  : std::fabs(height - bestHeight) <= kEpsilon;
            if (!better && found && tied) better = triangle < bestTriangle;
            if (!better) continue;
            found = true;
            bestHeight = height;
            bestDistance = distance;
            bestTriangle = triangle;
            bestA = a;
            bestB = b;
            bestC = c;
        }
    }
    if (!found)
        return meshFailure<PlacementSystem::PlacementHit>(
            eve::DiagnosticCode::NotFound,
            "no projectable mesh triangle exists at the plane coordinate", config_.surfaceId);

    std::array<Vec3, 3> p;
    std::array<Vec3, 3> n;
    for (uint32_t corner = 0; corner < 3; ++corner) {
        const size_t offset = static_cast<size_t>(indices_[bestTriangle * 3 + corner]) * 3;
        p[corner] = {vertices_[offset], vertices_[offset + 1], vertices_[offset + 2]};
        if (!normals_.empty())
            n[corner] = {normals_[offset], normals_[offset + 1], normals_[offset + 2]};
    }
    Vec3 normal = normals_.empty()
                      ? cross(subtract(p[1], p[0]), subtract(p[2], p[0]))
                      : Vec3{bestA * n[0].x + bestB * n[1].x + bestC * n[2].x,
                             bestA * n[0].y + bestB * n[1].y + bestC * n[2].y,
                             bestA * n[0].z + bestB * n[1].z + bestC * n[2].z};
    if (lengthSquared(normal) <= kEpsilon * kEpsilon)
        normal = cross(subtract(p[1], p[0]), subtract(p[2], p[0]));
    if (config_.orientNormalsToGridUp && (xz ? normal.y : normal.z) < 0.f) {
        normal.x = -normal.x;
        normal.y = -normal.y;
        normal.z = -normal.z;
    }
    Vec3 tangent = subtract(p[1], p[0]);
    if (lengthSquared(tangent) <= kEpsilon * kEpsilon) tangent = subtract(p[2], p[0]);

    PlacementSystem::PlacementHit hit;
    hit.worldX = planeX;
    hit.worldY = xz ? bestHeight : planeY;
    hit.worldZ = xz ? planeY : bestHeight;
    hit.normalX = normal.x;
    hit.normalY = normal.y;
    hit.normalZ = normal.z;
    hit.tangentX = tangent.x;
    hit.tangentY = tangent.y;
    hit.tangentZ = tangent.z;
    hit.surfaceId = config_.surfaceId;
    hit.surfaceRevision = config_.surfaceRevision;
    hit.primitiveId = bestTriangle;
    hit.tags = config_.tags;
    return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
}

}  // namespace eve::building
