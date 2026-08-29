#include "fluids/FluidSurfaceBinding.h"

#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace eve::fluids {
namespace {

struct EdgeKey {
    uint32_t a = 0;
    uint32_t b = 0;

    bool operator==(const EdgeKey& rhs) const { return a == rhs.a && b == rhs.b; }
};

struct EdgeHash {
    size_t operator()(const EdgeKey& edge) const {
        const size_t first = std::hash<uint32_t>{}(edge.a);
        const size_t second = std::hash<uint32_t>{}(edge.b);
        return first ^ (second + size_t(0x9e3779b9u) + (first << 6u) + (first >> 2u));
    }
};

struct EdgeOwner {
    int triangle      = -1;
    int oppositeVertex = -1;
};

EdgeKey edgeKey(uint32_t a, uint32_t b) { return {std::min(a, b), std::max(a, b)}; }

glm::vec3 closestPointBarycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b,
                                  const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float     d1 = glm::dot(ab, ap);
    const float     d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return {1.f, 0.f, 0.f};

    const glm::vec3 bp = p - b;
    const float     d3 = glm::dot(ab, bp);
    const float     d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return {0.f, 1.f, 0.f};

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float v = d1 / (d1 - d3);
        return {1.f - v, v, 0.f};
    }

    const glm::vec3 cp = p - c;
    const float     d5 = glm::dot(ab, cp);
    const float     d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return {0.f, 0.f, 1.f};

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float w = d2 / (d2 - d6);
        return {1.f - w, 0.f, w};
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && d4 - d3 >= 0.f && d5 - d6 >= 0.f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {0.f, 1.f - w, w};
    }

    const float inv = 1.f / (va + vb + vc);
    const float v   = vb * inv;
    const float w   = vc * inv;
    return {1.f - v - w, v, w};
}

}  // namespace

bool FluidSurfaceBinding::build(const std::vector<glm::vec3>& positions,
                                const std::vector<uint32_t>& indices,
                                const std::vector<glm::vec2>& uvs) {
    restPositions_.clear();
    currentPositions_.clear();
    previousPositions_.clear();
    indices_.clear();
    uvs_.clear();
    adjacency_.clear();
    const auto fail = [&]() {
        restPositions_.clear();
        currentPositions_.clear();
        previousPositions_.clear();
        indices_.clear();
        uvs_.clear();
        adjacency_.clear();
        return false;
    };
    if (positions.empty() || indices.empty() || indices.size() % 3u != 0u) return false;
    if (!uvs.empty() && uvs.size() != positions.size()) return false;
    for (uint32_t index : indices) if (index >= positions.size()) return false;

    restPositions_     = positions;
    currentPositions_  = positions;
    previousPositions_ = positions;
    indices_           = indices;
    uvs_               = uvs;
    adjacency_.assign(indices.size() / 3u, glm::ivec3(-1));

    std::unordered_map<EdgeKey, std::vector<EdgeOwner>, EdgeHash> edges;
    for (int tri = 0; tri < triangleCount(); ++tri) {
        const uint32_t base = uint32_t(tri) * 3u;
        const glm::vec3 ab = positions[indices_[base + 1u]] - positions[indices_[base]];
        const glm::vec3 ac = positions[indices_[base + 2u]] - positions[indices_[base]];
        if (glm::length2(glm::cross(ab, ac)) < 1e-16f) return fail();
        for (int opposite = 0; opposite < 3; ++opposite) {
            const uint32_t a = indices_[base + uint32_t((opposite + 1) % 3)];
            const uint32_t b = indices_[base + uint32_t((opposite + 2) % 3)];
            const EdgeKey  key = edgeKey(a, b);
            auto& owners = edges[key];
            owners.push_back({tri, opposite});
            if (owners.size() > 2u) return fail();
        }
    }
    for (const auto& [key, owners] : edges) {
        (void)key;
        if (owners.size() != 2u) continue;
        adjacency_[size_t(owners[0].triangle)][owners[0].oppositeVertex] = owners[1].triangle;
        adjacency_[size_t(owners[1].triangle)][owners[1].oppositeVertex] = owners[0].triangle;
    }
    return true;
}

void FluidSurfaceBinding::setTransform(const glm::mat4& transform) {
    if (!isValid()) return;
    previousPositions_ = currentPositions_;
    for (size_t i = 0; i < restPositions_.size(); ++i)
        currentPositions_[i] = glm::vec3(transform * glm::vec4(restPositions_[i], 1.f));
}

bool FluidSurfaceBinding::setDeformedPositions(const std::vector<glm::vec3>& worldPositions) {
    if (!isValid() || worldPositions.size() != currentPositions_.size()) return false;
    previousPositions_ = currentPositions_;
    currentPositions_  = worldPositions;
    return true;
}

void FluidSurfaceBinding::commitPose() { previousPositions_ = currentPositions_; }

bool FluidSurfaceBinding::isValid() const {
    return !currentPositions_.empty() && currentPositions_.size() == previousPositions_.size() &&
           indices_.size() >= 3u && indices_.size() % 3u == 0u;
}

int FluidSurfaceBinding::triangleCount() const { return int(indices_.size() / 3u); }

std::vector<glm::uvec3> FluidSurfaceBinding::triangles() const {
    std::vector<glm::uvec3> result;
    result.reserve(size_t(triangleCount()));
    for (size_t i = 0; i + 2u < indices_.size(); i += 3u)
        result.emplace_back(indices_[i], indices_[i + 1u], indices_[i + 2u]);
    return result;
}

glm::vec3 FluidSurfaceBinding::triangleNormal(uint32_t triangle) const {
    const uint32_t base = triangle * 3u;
    const glm::vec3& a = currentPositions_[indices_[base]];
    const glm::vec3& b = currentPositions_[indices_[base + 1u]];
    const glm::vec3& c = currentPositions_[indices_[base + 2u]];
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float n2 = glm::length2(n);
    return n2 > 1e-16f ? n / std::sqrt(n2) : glm::vec3(0.f, 1.f, 0.f);
}

glm::vec3 FluidSurfaceBinding::barycentric(uint32_t triangle, const glm::vec3& point) const {
    const uint32_t base = triangle * 3u;
    const glm::vec3& a = currentPositions_[indices_[base]];
    const glm::vec3& b = currentPositions_[indices_[base + 1u]];
    const glm::vec3& c = currentPositions_[indices_[base + 2u]];
    const glm::vec3 v0 = b - a;
    const glm::vec3 v1 = c - a;
    const glm::vec3 v2 = point - a;
    const float d00 = glm::dot(v0, v0);
    const float d01 = glm::dot(v0, v1);
    const float d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0);
    const float d21 = glm::dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1e-16f) return {1.f, 0.f, 0.f};
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return {1.f - v - w, v, w};
}

SurfaceSample FluidSurfaceBinding::evaluate(const SurfaceLocation& location, float dt) const {
    SurfaceSample sample;
    if (!isValid() || location.triangle >= uint32_t(triangleCount())) return sample;
    sample.location = location;
    const glm::vec3 bary = location.barycentric;
    const uint32_t base = location.triangle * 3u;
    for (int i = 0; i < 3; ++i) {
        const uint32_t vertex = indices_[base + uint32_t(i)];
        sample.position += currentPositions_[vertex] * bary[i];
        sample.previousPosition += previousPositions_[vertex] * bary[i];
        if (!uvs_.empty()) sample.uv += uvs_[vertex] * bary[i];
    }
    sample.normal = triangleNormal(location.triangle);
    const glm::vec3 edge = currentPositions_[indices_[base + 1u]] - currentPositions_[indices_[base]];
    const glm::vec3 tangent = edge - sample.normal * glm::dot(edge, sample.normal);
    const float t2 = glm::length2(tangent);
    sample.tangent = t2 > 1e-16f ? tangent / std::sqrt(t2) : glm::vec3(1.f, 0.f, 0.f);
    sample.bitangent = glm::normalize(glm::cross(sample.normal, sample.tangent));
    if (dt > 1e-8f) sample.velocity = (sample.position - sample.previousPosition) / dt;
    return sample;
}

bool FluidSurfaceBinding::project(const glm::vec3& worldPosition, float maxDistance,
                                  SurfaceLocation& outLocation) const {
    if (!isValid()) return false;
    float best = std::numeric_limits<float>::max();
    SurfaceLocation location;
    for (int tri = 0; tri < triangleCount(); ++tri) {
        const uint32_t base = uint32_t(tri) * 3u;
        const glm::vec3 bary = closestPointBarycentric(worldPosition,
            currentPositions_[indices_[base]], currentPositions_[indices_[base + 1u]],
            currentPositions_[indices_[base + 2u]]);
        const glm::vec3 point = currentPositions_[indices_[base]] * bary.x +
                                currentPositions_[indices_[base + 1u]] * bary.y +
                                currentPositions_[indices_[base + 2u]] * bary.z;
        const float distance2 = glm::distance2(worldPosition, point);
        if (distance2 < best) {
            best = distance2;
            location = {uint32_t(tri), bary};
        }
    }
    if (maxDistance >= 0.f && best > maxDistance * maxDistance) return false;
    outLocation = location;
    return true;
}

SurfaceWalkResult FluidSurfaceBinding::walkAcrossSurface(const SurfaceLocation& start,
                                                         const glm::vec3& worldDisplacement,
                                                         int maxCrossings) const {
    SurfaceWalkResult result;
    if (!isValid() || start.triangle >= uint32_t(triangleCount()) || maxCrossings < 0) return result;
    result.location = start;
    result.valid = true;
    glm::vec3 remaining = worldDisplacement;
    constexpr float epsilon = 1e-5f;

    for (int crossing = 0; crossing <= maxCrossings; ++crossing) {
        const SurfaceSample sample = evaluate(result.location, 0.f);
        remaining -= sample.normal * glm::dot(remaining, sample.normal);
        if (glm::length2(remaining) < epsilon * epsilon) {
            result.remainingDisplacement = glm::vec3(0.f);
            return result;
        }
        const glm::vec3 targetBary = barycentric(result.location.triangle, sample.position + remaining);
        int outside = -1;
        float mostNegative = -epsilon;
        for (int i = 0; i < 3; ++i) {
            if (targetBary[i] < mostNegative) {
                mostNegative = targetBary[i];
                outside = i;
            }
        }
        if (outside < 0) {
            result.location.barycentric = glm::max(targetBary, glm::vec3(0.f));
            result.location.barycentric /= result.location.barycentric.x + result.location.barycentric.y +
                                           result.location.barycentric.z;
            result.remainingDisplacement = glm::vec3(0.f);
            return result;
        }

        const float from = result.location.barycentric[outside];
        const float to = targetBary[outside];
        const float t = std::clamp(from / (from - to), 0.f, 1.f);
        glm::vec3 edgeBary = glm::mix(result.location.barycentric, targetBary, t);
        edgeBary[outside] = 0.f;
        edgeBary = glm::max(edgeBary, glm::vec3(0.f));
        edgeBary /= edgeBary.x + edgeBary.y + edgeBary.z;
        remaining *= 1.f - t;

        const int next = adjacentTriangle(result.location.triangle, outside);
        if (next < 0) {
            result.location.barycentric = edgeBary;
            result.remainingDisplacement = remaining;
            result.reachedBoundary = true;
            return result;
        }

        const SurfaceSample edgeSample = evaluate({result.location.triangle, edgeBary}, 0.f);
        result.location.triangle = uint32_t(next);
        result.location.barycentric = barycentric(uint32_t(next), edgeSample.position);
        result.location.barycentric = glm::max(result.location.barycentric, glm::vec3(0.f));
        result.location.barycentric /= result.location.barycentric.x + result.location.barycentric.y +
                                       result.location.barycentric.z;
        if (crossing == maxCrossings) {
            result.remainingDisplacement = remaining;
            return result;
        }
    }
    return result;
}

int FluidSurfaceBinding::adjacentTriangle(uint32_t triangle, int oppositeVertex) const {
    if (triangle >= adjacency_.size() || oppositeVertex < 0 || oppositeVertex > 2) return -1;
    return adjacency_[triangle][oppositeVertex];
}

}  // namespace eve::fluids
