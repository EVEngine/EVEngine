#include "fluids/FluidSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

void FluidSimulation::step(float dt) {
    if (count_ <= 0 || sdf_.voxelCount() <= 0) return;
    const int   iters = std::max(1, params_.iterations);
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
        const float C       = rho / rho0 - 1.f;
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
        glm::vec3   p = pi + delta / rho0;
        const float d = sdf_.sample(p);
        if (d < radius) {
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

        // XSPH viscosity + Akinci-style cohesion, one neighbor pass.
        if (params_.viscosity > 0.f || params_.cohesion > 0.f) {
            glm::vec3        viscAcc(0.f);
            glm::vec3        cohesionAcc(0.f);
            const glm::ivec3 c = grid_.cellOf(p);
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
                                if (params_.viscosity > 0.f)
                                    viscAcc += (particles_[size_t(j)].vel - v) * fluidPoly6(r2, h);
                                if (params_.cohesion > 0.f) {
                                    const float r = std::sqrt(r2);
                                    cohesionAcc += -params_.cohesion * fluidCohesionKernel(r, h) * (dx / r);
                                }
                            }
                        }
                    }
                }
            }
            v += viscAcc * (params_.viscosity * dt);
            v += cohesionAcc * dt;
        }

        v += params_.gravity * dt;
        v *= std::max(0.f, 1.f - params_.damping * dt);
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
        if (d < radius) {
            p              = p - n * (d - radius);
            const float vn = glm::dot(v, n);
            if (vn < 0.f) v -= n * vn;
            d = radius;
        }
        // Adhesion: pull the film toward the solid while within range.
        if (params_.adhesion > 0.f && d < h) v += -n * (params_.adhesion * fluidCohesionKernel(d, h) * dt);

        particles_[size_t(i)].pos = p;
        particles_[size_t(i)].vel = v;
    }
}

}  // namespace eve::fluids
