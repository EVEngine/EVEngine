#include "fluids/FluidSdf.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <unordered_map>
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"
#include "fluids/FluidSurfaceRenderer.h"

// FluidSdf.cpp
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
    int             hits      = 0;
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

// FluidSimulation.cpp
namespace eve::fluids {

SimGrid SimGrid::make(const MeshSdf& sdf, float cellSize) {
    SimGrid grid;
    grid.cellSize          = std::max(cellSize, 1e-4f);
    const glm::vec3 extent = glm::vec3(sdf.dims) * sdf.cellSize;
    const glm::vec3 cells  = glm::ceil((extent + glm::vec3(2.f * grid.cellSize)) / grid.cellSize);
    grid.origin            = sdf.origin - glm::vec3(grid.cellSize);
    grid.dims              = glm::max(glm::ivec3(1), glm::ivec3(cells));
    return grid;
}

int SimGrid::cellCount() const { return dims.x * dims.y * dims.z; }

int SimGrid::cellIndex(int x, int y, int z) const { return x + dims.x * (y + dims.y * z); }

bool SimGrid::inBounds(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < dims.x && y < dims.y && z < dims.z;
}

glm::ivec3 SimGrid::cellOf(const glm::vec3& p) const {
    glm::ivec3 c = glm::ivec3(glm::floor((p - origin) / cellSize));
    return glm::clamp(c, glm::ivec3(0), dims - glm::ivec3(1));
}

FluidSimulation::FluidSimulation(int maxParticles, const FluidParams& params)
    : params_(params), maxParticles_(std::max(1, maxParticles)) {
    particles_.resize(size_t(maxParticles_));
    densities_.resize(size_t(maxParticles_));
    lambdas_.resize(size_t(maxParticles_));
    gradSums_.resize(size_t(maxParticles_));
    cellNext_.resize(size_t(maxParticles_));
}

void FluidSimulation::setSdf(const MeshSdf& sdf) {
    sdf_  = sdf;
    grid_ = SimGrid::make(sdf_, params_.supportRadius);
    cellHead_.assign(size_t(grid_.cellCount()), -1);
}

void FluidSimulation::clear() {
    count_ = 0;
    std::fill(cellHead_.begin(), cellHead_.end(), -1);
}

int FluidSimulation::spawnDrop(const glm::vec3& center, float radius, int count) {
    int added = 0;
    for (int k = 0; k < count && count_ < maxParticles_; ++k) {
        // Deterministic golden-ratio sphere fill so tests are stable.
        const float z  = (float((0x9E3779B9u * uint32_t(k + 1)) % 10000u) / 10000.f) * 2.f - 1.f;
        const float a  = 2.399963229728653f * float(k);
        const float rr = radius * std::sqrt(std::max(0.f, 1.f - z * z));
        glm::vec3   p  = center + glm::vec3(std::cos(a) * rr, radius * z, std::sin(a) * rr);
        if (sdf_.voxelCount() > 0) {
            const float d = sdf_.sample(p);
            if (d < params_.particleRadius) {
                glm::vec3   n  = sdf_.gradient(p);
                const float nl = glm::length(n);
                if (nl > 1e-6f)
                    n /= nl;
                else
                    n = glm::vec3(0.f, 1.f, 0.f);
                p = p - n * (d - params_.particleRadius);
            }
        }
        particles_[size_t(count_)] = FluidParticle{p, glm::vec3(0.f)};
        densities_[size_t(count_)] = 0.f;
        ++count_;
        ++added;
    }
    return added;
}

void FluidSimulation::step(float dt) { step(dt, std::max(1, params_.iterations)); }

void FluidSimulation::step(float dt, int substeps) {
    if (count_ <= 0 || sdf_.voxelCount() <= 0) return;
    const int   iters = std::max(1, substeps);
    const float sub   = dt / float(iters);
    for (int i = 0; i < iters; ++i) {
        rebuildGrid();
        integrate(sub);
        const int pbf = std::max(0, params_.pbfIterations);
        for (int k = 0; k < pbf; ++k) {
            rebuildGrid();
            computeDensitiesAndGrads();
            computeLambdas();
            applyPositionCorrections();
        }
    }
}

void FluidSimulation::rebuildGrid() {
    if (cellHead_.empty()) return;
    std::fill(cellHead_.begin(), cellHead_.end(), -1);
    for (int i = 0; i < count_; ++i) {
        const glm::ivec3 c      = grid_.cellOf(particles_[size_t(i)].pos);
        const int        cell   = grid_.cellIndex(c.x, c.y, c.z);
        cellNext_[size_t(i)]    = cellHead_[size_t(cell)];
        cellHead_[size_t(cell)] = i;
    }
}

void FluidSimulation::computeDensitiesAndGrads() {
    const float h  = params_.supportRadius;
    const float h2 = h * h;
    for (int i = 0; i < count_; ++i) {
        const glm::vec3  pi   = particles_[size_t(i)].pos;
        const glm::ivec3 c    = grid_.cellOf(pi);
        float            dens = 0.f;
        glm::vec3        gradSum(0.f);
        for (int z = -1; z <= 1; ++z) {
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    const int cx = c.x + x;
                    const int cy = c.y + y;
                    const int cz = c.z + z;
                    if (!grid_.inBounds(cx, cy, cz)) continue;
                    const int cell = grid_.cellIndex(cx, cy, cz);
                    for (int j = cellHead_[size_t(cell)]; j >= 0; j = cellNext_[size_t(j)]) {
                        const glm::vec3 dx = pi - particles_[size_t(j)].pos;
                        const float     r2 = glm::dot(dx, dx);
                        if (r2 < h2) {
                            dens += fluidPoly6(r2, h);
                            gradSum += fluidSpikyGrad(dx, h);
                        }
                    }
                }
            }
        }
        densities_[size_t(i)] = dens;
        gradSums_[size_t(i)]  = gradSum;
    }
}

void FluidSimulation::computeLambdas() {
    const float rho0 = std::max(params_.restDensity, 1e-6f);
    for (int i = 0; i < count_; ++i) {
        const float rho     = densities_[size_t(i)];
        const float C       = std::max(0.f, rho / rho0 - 1.f);
        const float gradSq  = glm::dot(gradSums_[size_t(i)], gradSums_[size_t(i)]);
        lambdas_[size_t(i)] = -C / (gradSq + 1e-6f);
    }
}

void FluidSimulation::applyPositionCorrections() {
    const float h      = params_.supportRadius;
    const float h2     = h * h;
    const float rho0   = std::max(params_.restDensity, 1e-6f);
    const float radius = params_.particleRadius;
    for (int i = 0; i < count_; ++i) {
        const glm::vec3  pi = particles_[size_t(i)].pos;
        const float      li = lambdas_[size_t(i)];
        const glm::ivec3 c  = grid_.cellOf(pi);
        glm::vec3        delta(0.f);
        for (int z = -1; z <= 1; ++z) {
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    const int cx = c.x + x;
                    const int cy = c.y + y;
                    const int cz = c.z + z;
                    if (!grid_.inBounds(cx, cy, cz)) continue;
                    const int cell = grid_.cellIndex(cx, cy, cz);
                    for (int j = cellHead_[size_t(cell)]; j >= 0; j = cellNext_[size_t(j)]) {
                        if (j == i) continue;
                        const glm::vec3 dx = pi - particles_[size_t(j)].pos;
                        const float     r2 = glm::dot(dx, dx);
                        if (r2 < h2) delta += (li + lambdas_[size_t(j)]) * fluidSpikyGrad(dx, h);
                    }
                }
            }
        }
        glm::vec3 p = pi + delta / rho0;
        // Clamp the PBF position correction (standard PBF stabilization):
        // overlapping particles would otherwise amplify the spiky gradient
        // unboundedly and fling the drop apart.
        const float     maxDelta   = 0.0005f * h;
        const glm::vec3 correction = delta / rho0;
        const float     cl         = glm::length(correction);
        if (cl > maxDelta && cl > 1e-9f) p = pi + correction * (maxDelta / cl);
        const float d = sdf_.sample(p);
        {
            glm::vec3   n  = sdf_.gradient(p);
            const float nl = glm::length(n);
            if (nl > 1e-6f)
                n /= nl;
            else
                n = glm::vec3(0.f, 1.f, 0.f);
            p = p - n * (d - radius);
        }
        particles_[size_t(i)].pos = p;
    }
}

void FluidSimulation::integrate(float dt) {
    const float h      = params_.supportRadius;
    const float h2     = h * h;
    const float radius = params_.particleRadius;
    for (int i = 0; i < count_; ++i) {
        glm::vec3 p = particles_[size_t(i)].pos;
        glm::vec3 v = particles_[size_t(i)].vel;

        // XSPH viscosity + Bingham yield stress + Akinci-style cohesion.
        if (params_.viscosity > 0.f || params_.cohesion > 0.f || params_.yieldStress > 0.f) {
            glm::vec3        viscAcc(0.f);
            glm::vec3        cohesionAcc(0.f);
            float            shearAcc       = 0.f;
            float            neighborWeight = 0.f;
            float            cohesionWeight = 0.f;
            const float      particleVolume = std::pow(2.f * radius, 3.f);
            const glm::ivec3 c              = grid_.cellOf(p);
            for (int z = -1; z <= 1; ++z) {
                for (int y = -1; y <= 1; ++y) {
                    for (int x = -1; x <= 1; ++x) {
                        const int cx = c.x + x;
                        const int cy = c.y + y;
                        const int cz = c.z + z;
                        if (!grid_.inBounds(cx, cy, cz)) continue;
                        const int cell = grid_.cellIndex(cx, cy, cz);
                        for (int j = cellHead_[size_t(cell)]; j >= 0; j = cellNext_[size_t(j)]) {
                            if (j == i) continue;
                            const glm::vec3 dx = p - particles_[size_t(j)].pos;
                            const float     r2 = glm::dot(dx, dx);
                            if (r2 < h2) {
                                const float weight = fluidPoly6(r2, h) * particleVolume;
                                if (params_.viscosity > 0.f || params_.yieldStress > 0.f) {
                                    viscAcc += (particles_[size_t(j)].vel - v) * weight;
                                    neighborWeight += weight;
                                }
                                if (params_.yieldStress > 0.f)
                                    shearAcc += glm::length(particles_[size_t(j)].vel - v) * weight;
                                if (params_.cohesion > 0.f) {
                                    cohesionAcc -= dx * weight;
                                    cohesionWeight += weight;
                                }
                            }
                        }
                    }
                }
            }
            // Bingham plastic: effective viscosity grows as shear rate drops,
            // so low-shear mud "freezes" and piles up instead of spreading.
            float effectiveViscosity = params_.viscosity;
            if (params_.yieldStress > 0.f && shearAcc > 1e-6f) effectiveViscosity += params_.yieldStress / shearAcc;
            if (neighborWeight > 1e-6f) v += (viscAcc / neighborWeight) * std::clamp(effectiveViscosity * dt, 0.f, 1.f);
            glm::vec3 cohesionNormal = sdf_.gradient(p);
            if (glm::length(cohesionNormal) > 1e-6f)
                cohesionNormal = glm::normalize(cohesionNormal);
            else
                cohesionNormal = glm::vec3(0.f, 1.f, 0.f);
            cohesionAcc -= cohesionNormal * glm::dot(cohesionAcc, cohesionNormal);
            if (cohesionWeight > 1e-6f)
                v += fluidClampSpeed((cohesionAcc / cohesionWeight) * (params_.cohesion * 100.f * dt), radius);
        }

        v += params_.gravity * dt;
        v *= std::max(0.f, 1.f - params_.damping * dt);
        if (params_.adhesion > 0.f) v *= std::exp(-params_.adhesion * 300.f * dt);
        v = fluidClampSpeed(v, params_.maxVelocity);
        p += v * dt;

        // Project onto the solid surface and kill inward normal velocity so
        // gravity only drives tangential flow (water film stays on the model).
        float       d  = sdf_.sample(p);
        glm::vec3   n  = sdf_.gradient(p);
        const float nl = glm::length(n);
        if (nl > 1e-6f)
            n /= nl;
        else
            n = glm::vec3(0.f, 1.f, 0.f);
        {
            p              = p - n * (d - radius);
            const float vn = glm::dot(v, n);
            v -= n * vn;
            d = radius;
        }
        // Adhesion: pull the film toward the solid while within range.
        if (params_.adhesion > 0.f && d < h) v += -n * (params_.adhesion * std::max(0.f, 1.f - d / h) * dt * 30.f);

        particles_[size_t(i)].pos = p;
        particles_[size_t(i)].vel = v;
    }
}

}  // namespace eve::fluids

// FluidSurfaceBinding.cpp
namespace eve::fluids {
namespace {

struct EdgeKey {
    uint32_t a = 0;
    uint32_t b = 0;

    bool operator==(const EdgeKey& rhs) const { return a == rhs.a && b == rhs.b; }
};

struct EdgeHash {
    size_t operator()(const EdgeKey& edge) const {
        const size_t first  = std::hash<uint32_t>{}(edge.a);
        const size_t second = std::hash<uint32_t>{}(edge.b);
        return first ^ (second + size_t(0x9e3779b9u) + (first << 6u) + (first >> 2u));
    }
};

struct EdgeOwner {
    int triangle       = -1;
    int oppositeVertex = -1;
};

EdgeKey edgeKey(uint32_t a, uint32_t b) { return {std::min(a, b), std::max(a, b)}; }

glm::vec3 closestPointBarycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
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

bool FluidSurfaceBinding::build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
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
    for (uint32_t index : indices)
        if (index >= positions.size()) return false;

    restPositions_     = positions;
    currentPositions_  = positions;
    previousPositions_ = positions;
    indices_           = indices;
    uvs_               = uvs;
    adjacency_.assign(indices.size() / 3u, glm::ivec3(-1));

    std::unordered_map<EdgeKey, std::vector<EdgeOwner>, EdgeHash> edges;
    for (int tri = 0; tri < triangleCount(); ++tri) {
        const uint32_t  base = uint32_t(tri) * 3u;
        const glm::vec3 ab   = positions[indices_[base + 1u]] - positions[indices_[base]];
        const glm::vec3 ac   = positions[indices_[base + 2u]] - positions[indices_[base]];
        if (glm::length2(glm::cross(ab, ac)) < 1e-16f) return fail();
        for (int opposite = 0; opposite < 3; ++opposite) {
            const uint32_t a      = indices_[base + uint32_t((opposite + 1) % 3)];
            const uint32_t b      = indices_[base + uint32_t((opposite + 2) % 3)];
            const EdgeKey  key    = edgeKey(a, b);
            auto&          owners = edges[key];
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
    const uint32_t   base = triangle * 3u;
    const glm::vec3& a    = currentPositions_[indices_[base]];
    const glm::vec3& b    = currentPositions_[indices_[base + 1u]];
    const glm::vec3& c    = currentPositions_[indices_[base + 2u]];
    const glm::vec3  n    = glm::cross(b - a, c - a);
    const float      n2   = glm::length2(n);
    return n2 > 1e-16f ? n / std::sqrt(n2) : glm::vec3(0.f, 1.f, 0.f);
}

glm::vec3 FluidSurfaceBinding::barycentric(uint32_t triangle, const glm::vec3& point) const {
    const uint32_t   base  = triangle * 3u;
    const glm::vec3& a     = currentPositions_[indices_[base]];
    const glm::vec3& b     = currentPositions_[indices_[base + 1u]];
    const glm::vec3& c     = currentPositions_[indices_[base + 2u]];
    const glm::vec3  v0    = b - a;
    const glm::vec3  v1    = c - a;
    const glm::vec3  v2    = point - a;
    const float      d00   = glm::dot(v0, v0);
    const float      d01   = glm::dot(v0, v1);
    const float      d11   = glm::dot(v1, v1);
    const float      d20   = glm::dot(v2, v0);
    const float      d21   = glm::dot(v2, v1);
    const float      denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1e-16f) return {1.f, 0.f, 0.f};
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return {1.f - v - w, v, w};
}

SurfaceSample FluidSurfaceBinding::evaluate(const SurfaceLocation& location, float dt) const {
    SurfaceSample sample;
    if (!isValid() || location.triangle >= uint32_t(triangleCount())) return sample;
    sample.location      = location;
    const glm::vec3 bary = location.barycentric;
    const uint32_t  base = location.triangle * 3u;
    for (int i = 0; i < 3; ++i) {
        const uint32_t vertex = indices_[base + uint32_t(i)];
        sample.position += currentPositions_[vertex] * bary[i];
        sample.previousPosition += previousPositions_[vertex] * bary[i];
        if (!uvs_.empty()) sample.uv += uvs_[vertex] * bary[i];
    }
    sample.normal           = triangleNormal(location.triangle);
    const glm::vec3 edge    = currentPositions_[indices_[base + 1u]] - currentPositions_[indices_[base]];
    const glm::vec3 tangent = edge - sample.normal * glm::dot(edge, sample.normal);
    const float     t2      = glm::length2(tangent);
    sample.tangent          = t2 > 1e-16f ? tangent / std::sqrt(t2) : glm::vec3(1.f, 0.f, 0.f);
    sample.bitangent        = glm::normalize(glm::cross(sample.normal, sample.tangent));
    if (dt > 1e-8f) sample.velocity = (sample.position - sample.previousPosition) / dt;
    return sample;
}

bool FluidSurfaceBinding::project(const glm::vec3& worldPosition, float maxDistance,
                                  SurfaceLocation& outLocation) const {
    if (!isValid()) return false;
    float           best = std::numeric_limits<float>::max();
    SurfaceLocation location;
    for (int tri = 0; tri < triangleCount(); ++tri) {
        const uint32_t  base = uint32_t(tri) * 3u;
        const glm::vec3 bary =
            closestPointBarycentric(worldPosition, currentPositions_[indices_[base]],
                                    currentPositions_[indices_[base + 1u]], currentPositions_[indices_[base + 2u]]);
        const glm::vec3 point = currentPositions_[indices_[base]] * bary.x +
                                currentPositions_[indices_[base + 1u]] * bary.y +
                                currentPositions_[indices_[base + 2u]] * bary.z;
        const float distance2 = glm::distance2(worldPosition, point);
        if (distance2 < best) {
            best     = distance2;
            location = {uint32_t(tri), bary};
        }
    }
    if (maxDistance >= 0.f && best > maxDistance * maxDistance) return false;
    outLocation = location;
    return true;
}

SurfaceWalkResult FluidSurfaceBinding::walkAcrossSurface(const SurfaceLocation& start,
                                                         const glm::vec3& worldDisplacement, int maxCrossings) const {
    SurfaceWalkResult result;
    if (!isValid() || start.triangle >= uint32_t(triangleCount()) || maxCrossings < 0) return result;
    result.location           = start;
    result.valid              = true;
    glm::vec3       remaining = worldDisplacement;
    constexpr float epsilon   = 1e-5f;

    for (int crossing = 0; crossing <= maxCrossings; ++crossing) {
        const SurfaceSample sample = evaluate(result.location, 0.f);
        remaining -= sample.normal * glm::dot(remaining, sample.normal);
        if (glm::length2(remaining) < epsilon * epsilon) {
            result.remainingDisplacement = glm::vec3(0.f);
            return result;
        }
        const glm::vec3 targetBary   = barycentric(result.location.triangle, sample.position + remaining);
        int             outside      = -1;
        float           mostNegative = -epsilon;
        for (int i = 0; i < 3; ++i) {
            if (targetBary[i] < mostNegative) {
                mostNegative = targetBary[i];
                outside      = i;
            }
        }
        if (outside < 0) {
            result.location.barycentric = glm::max(targetBary, glm::vec3(0.f));
            result.location.barycentric /=
                result.location.barycentric.x + result.location.barycentric.y + result.location.barycentric.z;
            result.remainingDisplacement = glm::vec3(0.f);
            return result;
        }

        const float from     = result.location.barycentric[outside];
        const float to       = targetBary[outside];
        const float t        = std::clamp(from / (from - to), 0.f, 1.f);
        glm::vec3   edgeBary = glm::mix(result.location.barycentric, targetBary, t);
        edgeBary[outside]    = 0.f;
        edgeBary             = glm::max(edgeBary, glm::vec3(0.f));
        edgeBary /= edgeBary.x + edgeBary.y + edgeBary.z;
        remaining *= 1.f - t;

        const int next = adjacentTriangle(result.location.triangle, outside);
        if (next < 0) {
            result.location.barycentric  = edgeBary;
            result.remainingDisplacement = remaining;
            result.reachedBoundary       = true;
            return result;
        }

        const SurfaceSample edgeSample = evaluate({result.location.triangle, edgeBary}, 0.f);
        result.location.triangle       = uint32_t(next);
        result.location.barycentric    = barycentric(uint32_t(next), edgeSample.position);
        result.location.barycentric    = glm::max(result.location.barycentric, glm::vec3(0.f));
        result.location.barycentric /=
            result.location.barycentric.x + result.location.barycentric.y + result.location.barycentric.z;
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

// FluidSurfaceBlend.cpp
namespace eve::fluids {

glm::vec4 FluidSurfaceRenderer::blendFluidColor(FluidBlendFactor factor, const glm::vec4& source,
                                                const glm::vec4& destination) {
    switch (factor) {
        case FluidBlendFactor::Zero: return glm::vec4(0.f);
        case FluidBlendFactor::One: return glm::vec4(1.f);
        case FluidBlendFactor::DestinationColor: return destination;
        case FluidBlendFactor::SourceColor: return source;
        case FluidBlendFactor::OneMinusDestinationColor: return glm::vec4(1.f) - destination;
        case FluidBlendFactor::SourceAlpha: return glm::vec4(source.a);
        case FluidBlendFactor::OneMinusSourceColor: return glm::vec4(1.f) - source;
        case FluidBlendFactor::DestinationAlpha: return glm::vec4(destination.a);
        case FluidBlendFactor::OneMinusDestinationAlpha: return glm::vec4(1.f - destination.a);
        case FluidBlendFactor::SourceAlphaSaturate: {
            const float saturated = std::min(source.a, 1.f - destination.a);
            return glm::vec4(saturated, saturated, saturated, 1.f);
        }
        case FluidBlendFactor::OneMinusSourceAlpha: return glm::vec4(1.f - source.a);
    }
    return glm::vec4(0.f);
}

Result<void> FluidSurfaceRenderer::configureSurfaceBlend(int source, int destination) {
    if (source < int(FluidBlendFactor::Zero) || source > int(FluidBlendFactor::OneMinusSourceAlpha) ||
        destination < int(FluidBlendFactor::Zero) || destination > int(FluidBlendFactor::OneMinusSourceAlpha))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Fluid surface blend factors must be Unity BlendMode values in [0,10]",
            "fluids.surface.blend"));
    surfaceBlendSource_      = static_cast<FluidBlendFactor>(source);
    surfaceBlendDestination_ = static_cast<FluidBlendFactor>(destination);
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeConfiguredSurfaceBlend(std::span<const uint8_t> sceneColor) {
    const size_t expected = size_t(params_.width) * size_t(params_.height) * 4u;
    if (!auxiliaryCurrent_ || residentColorCurrent_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Current fluid frame has no host-visible surface color",
                                                       "fluids.surface.blend"));
    if (sceneColor.size() != expected)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Surface blend requires a matching RGBA8 scene image",
                                                       "fluids.surface.blend"));

    for (size_t offset = 0; offset < expected; offset += 4u) {
        glm::vec4 source, destination;
        for (size_t channel = 0; channel < 4u; ++channel) {
            const auto component   = static_cast<glm::vec4::length_type>(channel);
            source[component]      = float(color_[offset + channel]) / 255.f;
            destination[component] = float(sceneColor[offset + channel]) / 255.f;
        }
        const glm::vec4 blended =
            glm::clamp(source * blendFluidColor(surfaceBlendSource_, source, destination) +
                           destination * blendFluidColor(surfaceBlendDestination_, source, destination),
                       glm::vec4(0.f), glm::vec4(1.f));
        for (size_t channel = 0; channel < 4u; ++channel)
            color_[offset + channel] = uint8_t(blended[static_cast<glm::vec4::length_type>(channel)] * 255.f + .5f);
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureParticleBlend(int source, int destination, bool depthWrite) {
    if (source < int(FluidBlendFactor::Zero) || source > int(FluidBlendFactor::OneMinusSourceAlpha) ||
        destination < int(FluidBlendFactor::Zero) || destination > int(FluidBlendFactor::OneMinusSourceAlpha))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Fluid particle blend factors must be Unity BlendMode values in [0,10]",
            "fluids.surface.particleBlend"));
    particleBlendSource_      = static_cast<FluidBlendFactor>(source);
    particleBlendDestination_ = static_cast<FluidBlendFactor>(destination);
    particleDepthWrite_       = depthWrite;
    particleBlendConfigured_  = true;
    return Result<void>::success();
}

}  // namespace eve::fluids
