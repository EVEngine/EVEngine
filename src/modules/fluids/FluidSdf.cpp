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

/** @brief Even-odd raycast along a skew direction; returns true when p is inside the mesh. */
bool pointInsideMesh(const glm::vec3& p, const std::vector<glm::vec3>& pos, const std::vector<uint32_t>& idx,
                     int triCount) {
    // Axis rays frequently pass through shared vertices/edges in symmetric meshes,
    // double-counting one crossing. A fixed irrational-looking direction preserves
    // deterministic baking while avoiding those systematic degeneracies.
    const glm::vec3 direction = glm::normalize(glm::vec3(1.f, .37139067f, .127831f));
    int hits = 0;
    for (int tri = 0; tri < triCount; ++tri) {
        const glm::vec3 a = pos[idx[uint32_t(tri) * 3 + 0]];
        const glm::vec3 b = pos[idx[uint32_t(tri) * 3 + 1]];
        const glm::vec3 c = pos[idx[uint32_t(tri) * 3 + 2]];
        // Moller-Trumbore ray from p along the fixed skew direction.
        const glm::vec3 e1  = b - a;
        const glm::vec3 e2  = c - a;
        const glm::vec3 h   = glm::cross(direction, e2);
        const float     det = glm::dot(e1, h);
        if (std::fabs(det) < 1e-12f) continue;
        const float     invDet = 1.f / det;
        const glm::vec3 s      = p - a;
        const float     u      = invDet * glm::dot(s, h);
        if (u < 0.f || u > 1.f) continue;
        const glm::vec3 q = glm::cross(s, e1);
        const float     v = invDet * glm::dot(direction, q);
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

float MeshSdf::sample(const glm::vec3& p) const { return sampleWithGradient(p).distance; }

MeshSdfSample MeshSdf::sampleWithGradient(const glm::vec3& p) const {
    if (dims.x < 2 || dims.y < 2 || dims.z < 2 || !(cellSize > 0.f) || distances.size() != size_t(voxelCount()))
        return {FLT_MAX, glm::vec3(0.f, 1.f, 0.f)};
    const glm::vec3  maximum      = origin + glm::vec3(dims - glm::ivec3(1)) * cellSize;
    const glm::vec3  clampedPoint = glm::clamp(p, origin, maximum);
    const glm::vec3  outside      = p - clampedPoint;
    const glm::vec3  f            = (clampedPoint - origin) / cellSize;
    const glm::ivec3 i0           = glm::min(glm::ivec3(glm::floor(f)), dims - glm::ivec3(2));
    const glm::ivec3 i1           = i0 + glm::ivec3(1);
    const glm::vec3  t            = glm::clamp(f - glm::vec3(i0), glm::vec3(0.f), glm::vec3(1.f));
    const float      v000         = distances[size_t(index(i0.x, i0.y, i0.z))];
    const float      v100         = distances[size_t(index(i1.x, i0.y, i0.z))];
    const float      v010         = distances[size_t(index(i0.x, i1.y, i0.z))];
    const float      v110         = distances[size_t(index(i1.x, i1.y, i0.z))];
    const float      v001         = distances[size_t(index(i0.x, i0.y, i1.z))];
    const float      v101         = distances[size_t(index(i1.x, i0.y, i1.z))];
    const float      v011         = distances[size_t(index(i0.x, i1.y, i1.z))];
    const float      v111         = distances[size_t(index(i1.x, i1.y, i1.z))];
    const auto       mix          = [](float a, float b, float u) { return a + (b - a) * u; };
    const float      x00 = mix(v000, v100, t.x), x10 = mix(v010, v110, t.x);
    const float      x01 = mix(v001, v101, t.x), x11 = mix(v011, v111, t.x);
    const float      y0 = mix(x00, x10, t.y), y1 = mix(x01, x11, t.y);
    const float      distance = mix(y0, y1, t.z);
    const float      dx = mix(mix(v100 - v000, v110 - v010, t.y), mix(v101 - v001, v111 - v011, t.y), t.z) / cellSize;
    const float      dy = mix(mix(v010 - v000, v110 - v100, t.x), mix(v011 - v001, v111 - v101, t.x), t.z) / cellSize;
    const float      dz = mix(mix(v001 - v000, v101 - v100, t.x), mix(v011 - v010, v111 - v110, t.x), t.y) / cellSize;
    const float      outsideDistance = glm::length(outside);
    return outsideDistance > 1e-7f ? MeshSdfSample{distance + outsideDistance, outside / outsideDistance}
                                   : MeshSdfSample{distance, {dx, dy, dz}};
}

glm::vec3 MeshSdf::gradient(const glm::vec3& p) const { return sampleWithGradient(p).gradient; }

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
                    // The narrow triangle sweep leaves deep interior and far exterior
                    // cells untouched. Resolve those cells exactly once during baking;
                    // substituting one cell width destroys the SDF magnitude and makes
                    // collision projection depend on grid resolution.
                    float distance = FLT_MAX;
                    for (int tri = 0; tri < triCount; ++tri) {
                        const glm::vec3 a = positions[indices[uint32_t(tri) * 3 + 0]];
                        const glm::vec3 b = positions[indices[uint32_t(tri) * 3 + 1]];
                        const glm::vec3 c = positions[indices[uint32_t(tri) * 3 + 2]];
                        distance          = std::min(distance, triangleDistance(p, a, b, c));
                    }
                    sdf.distances[i] = distance;
                }
                if (pointInsideMesh(p, positions, indices, triCount)) {
                    sdf.distances[i] = -sdf.distances[i];
                }
            }
        }
    }
    return sdf;
}

}  // namespace eve::fluids
