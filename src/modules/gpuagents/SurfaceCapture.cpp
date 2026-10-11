#include "gpuagents/SurfaceCapture.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {
namespace {

bool pointInTriangle2D(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, float& w0,
                       float& w1, float& w2) {
    const glm::vec2 v0  = b - a;
    const glm::vec2 v1  = c - a;
    const glm::vec2 v2  = p - a;
    const float     den = v0.x * v1.y - v1.x * v0.y;
    if (std::abs(den) < 1e-12f) return false;
    const float inv = 1.f / den;
    w1              = (v2.x * v1.y - v1.x * v2.y) * inv;
    w2              = (v0.x * v2.y - v2.x * v0.y) * inv;
    w0              = 1.f - w1 - w2;
    return w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f;
}

}  // namespace

void SurfaceCapture::rebuildNormals(SurfaceField& field) {
    if (field.height.empty() || field.resolution < 2) return;
    const int   res  = field.resolution;
    const float cell = field.worldSize / static_cast<float>(res - 1);
    field.normals.assign(field.height.size(), glm::vec3(0.f, 1.f, 0.f));
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const int       x0  = std::max(x - 1, 0);
            const int       x1  = std::min(x + 1, res - 1);
            const int       z0  = std::max(z - 1, 0);
            const int       z1  = std::min(z + 1, res - 1);
            const float     hx0 = field.height[static_cast<size_t>(x0 + res * z)];
            const float     hx1 = field.height[static_cast<size_t>(x1 + res * z)];
            const float     hz0 = field.height[static_cast<size_t>(x + res * z0)];
            const float     hz1 = field.height[static_cast<size_t>(x + res * z1)];
            const glm::vec3 dx(2.f * cell, hx1 - hx0, 0.f);
            const glm::vec3 dz(0.f, hz1 - hz0, 2.f * cell);
            glm::vec3       n   = glm::cross(dz, dx);
            const float     len = glm::length(n);
            if (len > 1e-6f)
                n /= len;
            else
                n = glm::vec3(0.f, 1.f, 0.f);
            field.normals[static_cast<size_t>(x + res * z)] = n;
        }
    }
}

Result<void> SurfaceCapture::captureFromTriangles(SurfaceField& out, std::span<const glm::vec3> positions,
                                                  std::span<const std::uint32_t> indices, const glm::vec3& originMin,
                                                  float worldSize, int resolution, float baseHeight) {
    if (resolution < 2) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "surface capture resolution must be >= 2", "resolution", {}, "gpuagents"));
    }
    if (!(worldSize > 0.f)) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "surface capture worldSize must be positive", "worldSize", {},
                                                       "gpuagents"));
    }
    if (indices.size() % 3 != 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "triangle index count must be a multiple of 3", "indices", {},
                                                       "gpuagents"));
    }
    for (std::uint32_t idx : indices) {
        if (idx >= positions.size()) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "triangle index out of range", "indices", {}, "gpuagents"));
        }
    }

    out.initFlat(originMin, worldSize, resolution, baseHeight);
    const float cell = worldSize / static_cast<float>(resolution - 1);

    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        const glm::vec3& A = positions[indices[t]];
        const glm::vec3& B = positions[indices[t + 1]];
        const glm::vec3& C = positions[indices[t + 2]];
        const glm::vec2  a(A.x, A.z), b(B.x, B.z), c(C.x, C.z);
        const float      minX = std::min({a.x, b.x, c.x});
        const float      maxX = std::max({a.x, b.x, c.x});
        const float      minZ = std::min({a.y, b.y, c.y});
        const float      maxZ = std::max({a.y, b.y, c.y});
        const int        x0 = std::clamp(static_cast<int>(std::floor((minX - originMin.x) / cell)), 0, resolution - 1);
        const int        x1 = std::clamp(static_cast<int>(std::ceil((maxX - originMin.x) / cell)), 0, resolution - 1);
        const int        z0 = std::clamp(static_cast<int>(std::floor((minZ - originMin.z) / cell)), 0, resolution - 1);
        const int        z1 = std::clamp(static_cast<int>(std::ceil((maxZ - originMin.z) / cell)), 0, resolution - 1);

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const glm::vec2 p(originMin.x + cell * static_cast<float>(x),
                                  originMin.z + cell * static_cast<float>(z));
                float           w0 = 0.f, w1 = 0.f, w2 = 0.f;
                if (!pointInTriangle2D(p, a, b, c, w0, w1, w2)) continue;
                const float h    = w0 * A.y + w1 * B.y + w2 * C.y;
                auto&       dest = out.height[static_cast<size_t>(x + resolution * z)];
                dest             = std::max(dest, h);
            }
        }
    }

    rebuildNormals(out);
    return Result<void>::success();
}

}  // namespace eve::gpuagents
