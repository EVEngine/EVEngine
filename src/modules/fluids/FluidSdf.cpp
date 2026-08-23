#include "fluids/FluidSdf.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

namespace eve::fluids {
namespace {

/** @brief Signed distance from a point to a triangle (flat, no sign). */
float triangleDistance(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float     d1 = glm::dot(ab, ap);
    const float     d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return glm::length(p - a);

    const glm::vec3 bp = p - b;
    const float     d3 = glm::dot(ab, bp);
    const float     d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return glm::length(p - b);

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float t = d1 / (d1 - d3);
        return glm::length(p - (a + ab * t));
    }

    const glm::vec3 cp = p - c;
    const float     d5 = glm::dot(ab, cp);
    const float     d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return glm::length(p - c);

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float t = d2 / (d2 - d6);
        return glm::length(p - (a + ac * t));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && d4 - d3 >= 0.f && d5 - d6 >= 0.f) {
        const float t2 = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return glm::length(p - (b + (c - b) * t2));
    }

    const float denom = 1.f / (va + vb + vc);
    const float v     = vb * denom;
    const float w     = vc * denom;
    return glm::length(p - (a + ab * v + ac * w));
}

/** @brief Even-odd raycast along +X; returns true when p is inside the mesh. */
bool pointInsideMesh(const glm::vec3& p, const std::vector<glm::vec3>& pos, const std::vector<uint32_t>& idx,
                     int triCount) {
    int hits = 0;
    for (int tri = 0; tri < triCount; ++tri) {
        const glm::vec3 a = pos[idx[uint32_t(tri) * 3 + 0]];
        const glm::vec3 b = pos[idx[uint32_t(tri) * 3 + 1]];
        const glm::vec3 c = pos[idx[uint32_t(tri) * 3 + 2]];
        // Moller-Trumbore ray from p along +X.
        const glm::vec3 e1  = b - a;
        const glm::vec3 e2  = c - a;
        const glm::vec3 h   = glm::cross(glm::vec3(1.f, 0.f, 0.f), e2);
        const float     det = glm::dot(e1, h);
        if (std::fabs(det) < 1e-12f) continue;
        const float     invDet = 1.f / det;
        const glm::vec3 s      = p - a;
        const float     u      = invDet * glm::dot(s, h);
        if (u < 0.f || u > 1.f) continue;
        const glm::vec3 q = glm::cross(s, e1);
        const float     v = invDet * glm::dot(glm::vec3(1.f, 0.f, 0.f), q);
        if (v < 0.f || u + v > 1.f) continue;
        const float t = invDet * glm::dot(e2, q);
        if (t > 1e-8f) ++hits;
    }
    return (hits & 1) != 0;
}

}  // namespace

int MeshSdf::voxelCount() const { return dims.x * dims.y * dims.z; }

int MeshSdf::index(int x, int y, int z) const { return x + dims.x * (y + dims.y * z); }

bool MeshSdf::inBounds(const glm::ivec3& c) const {
    return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < dims.x && c.y < dims.y && c.z < dims.z;
}

float MeshSdf::sample(const glm::vec3& p) const {
    if (voxelCount() <= 0) return FLT_MAX;
    const glm::vec3  f  = (p - origin) / cellSize;
    const glm::ivec3 i0 = glm::clamp(glm::ivec3(glm::floor(f)), glm::ivec3(0), dims - glm::ivec3(1));
    const glm::ivec3 i1 = glm::min(i0 + glm::ivec3(1), dims - glm::ivec3(1));
    const glm::vec3  t  = glm::fract(f);

    const float v000 = distances[index(i0.x, i0.y, i0.z)];
    const float v100 = distances[index(i1.x, i0.y, i0.z)];
    const float v010 = distances[index(i0.x, i1.y, i0.z)];
    const float v110 = distances[index(i1.x, i1.y, i0.z)];
    const float v001 = distances[index(i0.x, i0.y, i1.z)];
    const float v101 = distances[index(i1.x, i0.y, i1.z)];
    const float v011 = distances[index(i0.x, i1.y, i1.z)];
    const float v111 = distances[index(i1.x, i1.y, i1.z)];

    const float c00 = v000 + (v100 - v000) * t.x;
    const float c10 = v010 + (v110 - v010) * t.x;
    const float c01 = v001 + (v101 - v001) * t.x;
    const float c11 = v011 + (v111 - v011) * t.x;
    const float c0  = c00 + (c10 - c00) * t.y;
    const float c1  = c01 + (c11 - c01) * t.y;
    return c0 + (c1 - c0) * t.z;
}

glm::vec3 MeshSdf::gradient(const glm::vec3& p) const {
    const float e = cellSize;
    return glm::vec3(sample(p + glm::vec3(e, 0.f, 0.f)) - sample(p - glm::vec3(e, 0.f, 0.f)),
                     sample(p + glm::vec3(0.f, e, 0.f)) - sample(p - glm::vec3(0.f, e, 0.f)),
                     sample(p + glm::vec3(0.f, 0.f, e)) - sample(p - glm::vec3(0.f, 0.f, e))) /
           (2.f * e);
}

MeshSdf MeshSdf::makeSphere(const glm::vec3& center, float radius, const glm::ivec3& dims) {
    MeshSdf sdf;
    sdf.dims           = dims;
    const float margin = radius * 0.5f;
    const float extent = 2.f * (radius + margin);
    sdf.cellSize       = extent / float(dims.x);
    sdf.origin         = center - glm::vec3(radius + margin);
    sdf.distances.resize(size_t(sdf.voxelCount()));
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                const glm::vec3 p = sdf.origin + glm::vec3(float(x), float(y), float(z)) * sdf.cellSize;
                sdf.distances[size_t(sdf.index(x, y, z))] = glm::length(p - center) - radius;
            }
        }
    }
    return sdf;
}

MeshSdf MeshSdf::makePlane(float planeY, const glm::ivec3& dims, float halfExtent) {
    MeshSdf sdf;
    sdf.dims     = dims;
    sdf.cellSize = (2.f * halfExtent) / float(dims.x);
    sdf.origin   = glm::vec3(-halfExtent, planeY - halfExtent, -halfExtent);
    sdf.distances.resize(size_t(sdf.voxelCount()));
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                const glm::vec3 p = sdf.origin + glm::vec3(float(x), float(y), float(z)) * sdf.cellSize;
                sdf.distances[size_t(sdf.index(x, y, z))] = p.y - planeY;
            }
        }
    }
    return sdf;
}

MeshSdf MeshSdf::makeFromTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                   const glm::ivec3& dims) {
    const int triCount = int(indices.size()) / 3;
    glm::vec3 minP(FLT_MAX);
    glm::vec3 maxP(-FLT_MAX);
    for (const glm::vec3& v : positions) {
        minP = glm::min(minP, v);
        maxP = glm::max(maxP, v);
    }
    const glm::vec3 extent = maxP - minP;
    const glm::vec3 pad    = extent * 0.25f + glm::vec3(1e-3f);
    const glm::vec3 total  = extent + pad * 2.f;
    const float     cell   = std::max({total.x / float(dims.x), total.y / float(dims.y), total.z / float(dims.z)});

    MeshSdf sdf;
    sdf.dims     = dims;
    sdf.cellSize = cell;
    sdf.origin   = minP - pad;
    sdf.distances.assign(size_t(sdf.voxelCount()), FLT_MAX);

    // Sweep each triangle over its expanded AABB and keep the min distance.
    for (int t = 0; t < triCount; ++t) {
        const glm::vec3  a    = positions[indices[uint32_t(t) * 3 + 0]];
        const glm::vec3  b    = positions[indices[uint32_t(t) * 3 + 1]];
        const glm::vec3  c    = positions[indices[uint32_t(t) * 3 + 2]];
        const glm::vec3  tmin = glm::min(a, glm::min(b, c));
        const glm::vec3  tmax = glm::max(a, glm::max(b, c));
        const glm::ivec3 c0   = glm::clamp(glm::ivec3(glm::floor((tmin - sdf.origin) / cell)) - glm::ivec3(1),
                                           glm::ivec3(0), dims - glm::ivec3(1));
        const glm::ivec3 c1   = glm::clamp(glm::ivec3(glm::floor((tmax - sdf.origin) / cell)) + glm::ivec3(1),
                                           glm::ivec3(0), dims - glm::ivec3(1));
        for (int z = c0.z; z <= c1.z; ++z) {
            for (int y = c0.y; y <= c1.y; ++y) {
                for (int x = c0.x; x <= c1.x; ++x) {
                    const glm::vec3 p    = sdf.origin + glm::vec3(float(x), float(y), float(z)) * cell;
                    const float     d    = triangleDistance(p, a, b, c);
                    float&          slot = sdf.distances[size_t(sdf.index(x, y, z))];
                    slot                 = std::min(slot, d);
                }
            }
        }
    }

    // Sign from even-odd raycast per voxel center.
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                const glm::vec3 p = sdf.origin + glm::vec3(float(x), float(y), float(z)) * cell;
                const size_t    i = size_t(sdf.index(x, y, z));
                if (sdf.distances[i] >= FLT_MAX) {
                    // No triangle in range: sign still matters far outside.
                    sdf.distances[i] = pointInsideMesh(p, positions, indices, triCount) ? -cell : cell;
                } else if (pointInsideMesh(p, positions, indices, triCount)) {
                    sdf.distances[i] = -sdf.distances[i];
                }
            }
        }
    }
    return sdf;
}

}  // namespace eve::fluids
