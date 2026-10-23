#include "gpuagents/SurfaceCapture.h"
#include <algorithm>
#include <cmath>
#include "common/Assert.h"
#include "common/Diagnostic.h"
#include "gpuagents/SurfaceField.h"

// SurfaceCapture.cpp
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

// SurfaceField.cpp
namespace eve::gpuagents {

void SurfaceField::initFlat(const glm::vec3& originMin, float size, int res, float planeY) {
    EV_PARAM_CHECK(res > 1);
    EV_PARAM_CHECK(size > 0.f);
    origin         = originMin;
    worldSize      = size;
    resolution     = res;
    const size_t n = static_cast<size_t>(res) * static_cast<size_t>(res);
    height.assign(n, planeY);
    normals.assign(n, glm::vec3(0.f, 1.f, 0.f));
    lifeField.assign(n, glm::vec4(0.f));
}

glm::vec2 SurfaceField::worldToUv(const glm::vec3& p) const {
    return glm::vec2((p.x - origin.x) / worldSize, (p.z - origin.z) / worldSize);
}

int SurfaceField::index(int x, int z) const { return x + resolution * z; }

bool SurfaceField::inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < resolution && z < resolution; }

float SurfaceField::sampleHeight(float x, float z) const {
    if (height.empty()) return 0.f;
    const glm::vec2 uv  = worldToUv(glm::vec3(x, 0.f, z));
    const float     fx  = uv.x * static_cast<float>(resolution - 1);
    const float     fz  = uv.y * static_cast<float>(resolution - 1);
    const int       x0  = std::clamp(static_cast<int>(std::floor(fx)), 0, resolution - 1);
    const int       z0  = std::clamp(static_cast<int>(std::floor(fz)), 0, resolution - 1);
    const int       x1  = std::min(x0 + 1, resolution - 1);
    const int       z1  = std::min(z0 + 1, resolution - 1);
    const float     tx  = fx - static_cast<float>(x0);
    const float     tz  = fz - static_cast<float>(z0);
    const float     h00 = height[static_cast<size_t>(index(x0, z0))];
    const float     h10 = height[static_cast<size_t>(index(x1, z0))];
    const float     h01 = height[static_cast<size_t>(index(x0, z1))];
    const float     h11 = height[static_cast<size_t>(index(x1, z1))];
    return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);
}

glm::vec3 SurfaceField::sampleNormal(float x, float z) const {
    if (normals.empty()) return glm::vec3(0.f, 1.f, 0.f);
    const glm::vec2 uv = worldToUv(glm::vec3(x, 0.f, z));
    const int       ix = std::clamp(static_cast<int>(std::floor(uv.x * resolution)), 0, resolution - 1);
    const int       iz = std::clamp(static_cast<int>(std::floor(uv.y * resolution)), 0, resolution - 1);
    return normals[static_cast<size_t>(index(ix, iz))];
}

glm::vec4 SurfaceField::sampleLifeTexel(float u, float v) const {
    const float fx = u * static_cast<float>(resolution - 1);
    const float fz = v * static_cast<float>(resolution - 1);
    const int   x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, resolution - 1);
    const int   z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, resolution - 1);
    const int   x1 = std::min(x0 + 1, resolution - 1);
    const int   z1 = std::min(z0 + 1, resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const auto  at = [&](int x, int z) { return lifeField[static_cast<size_t>(index(x, z))]; };
    return glm::mix(glm::mix(at(x0, z0), at(x1, z0), tx), glm::mix(at(x0, z1), at(x1, z1), tx), tz);
}

glm::vec4 SurfaceField::sampleLife(float x, float z) const {
    if (lifeField.empty()) return glm::vec4(0.f);
    const glm::vec2 uv = worldToUv(glm::vec3(x, 0.f, z));
    return sampleLifeTexel(std::clamp(uv.x, 0.f, 1.f), std::clamp(uv.y, 0.f, 1.f));
}

glm::vec3 SurfaceField::project(const glm::vec3& p) const { return glm::vec3(p.x, sampleHeight(p.x, p.z), p.z); }

void SurfaceField::stepLife(float dt, float decayRate, float halfLife, float diffusion,
                            const std::vector<glm::vec3>& deposits, float depositStrength) {
    if (lifeField.empty()) return;
    EV_PARAM_CHECK(dt >= 0.f);
    const float decay      = std::exp(-decayRate * dt);
    const float freshDecay = halfLife > 1e-4f ? std::exp(-std::log(2.f) * dt / halfLife) : 0.f;

    std::vector<glm::vec4> next = lifeField;
    const int              res  = resolution;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i = static_cast<size_t>(index(x, z));
            glm::vec4    c = lifeField[i];
            c.r *= decay;
            c.a *= freshDecay;
            if (diffusion > 0.f) {
                glm::vec4 sum(0.f);
                int       count = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dz == 0) continue;
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (!inBounds(nx, nz)) continue;
                        sum += lifeField[static_cast<size_t>(index(nx, nz))];
                        ++count;
                    }
                }
                if (count > 0) {
                    const glm::vec4 avg = sum / static_cast<float>(count);
                    c.r                 = glm::mix(c.r, avg.r, diffusion);
                    c.g                 = glm::mix(c.g, avg.g, diffusion * 0.5f);
                    c.b                 = glm::mix(c.b, avg.b, diffusion * 0.25f);
                }
            }
            next[i] = c;
        }
    }

    for (const glm::vec3& p : deposits) {
        const glm::vec2 uv = worldToUv(p);
        if (uv.x < 0.f || uv.y < 0.f || uv.x >= 1.f || uv.y >= 1.f) continue;
        const int x = std::clamp(static_cast<int>(uv.x * res), 0, res - 1);
        const int z = std::clamp(static_cast<int>(uv.y * res), 0, res - 1);
        auto&     c = next[static_cast<size_t>(index(x, z))];
        c.r         = std::min(c.r + depositStrength, 1.f);
        c.a         = 1.f;
    }
    lifeField.swap(next);
}

}  // namespace eve::gpuagents
