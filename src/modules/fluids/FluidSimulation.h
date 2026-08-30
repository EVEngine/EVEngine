#pragma once

/**
 * @brief CPU reference fluid solver for surface flows.
 *
 * This is the executable spec for the GPU kernels in FluidGpuKernels: uniform
 * grid neighbor search, SPH density, PBF density constraints, XSPH viscosity,
 * Akinci-style cohesion/adhesion, gravity, surface projection through the mesh
 * SDF and velocity decomposition. Tests exercise it numerically; the GLSL
 * kernels mirror it 1:1.
 */

#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::fluids {

/** @brief Uniform grid used for neighbor queries. */
struct SimGrid {
    /** @brief World position of cell (0,0,0). */
    glm::vec3 origin{0.f};
    /** @brief Cell counts per axis. */
    glm::ivec3 dims{0};
    /** @brief Cell size in world units (== SPH support radius). */
    float cellSize = 1.f;

    /** @brief Build a grid covering the SDF domain plus one cell of padding. */
    static SimGrid make(const MeshSdf& sdf, float cellSize);

    /** @return dims.x * dims.y * dims.z. */
    int cellCount() const;

    /** @return flat index of cell (x,y,z); caller guarantees bounds. */
    int cellIndex(int x, int y, int z) const;

    /** @return true when the cell coordinate is inside the grid. */
    bool inBounds(int x, int y, int z) const;

    /** @return clamped cell coordinate of a world position. */
    glm::ivec3 cellOf(const glm::vec3& p) const;
};

/** @brief CPU reference surface-flow solver. */
class FluidSimulation {
public:
    /**
     * @param maxParticles capacity of the particle buffer.
     * @param params tuning parameters.
     */
    explicit FluidSimulation(int maxParticles, const FluidParams& params = FluidParams{});

    /** @brief Replace the collision surface. Particles keep their state. */
    void setSdf(const MeshSdf& sdf);

    /** @return the currently bound SDF. */
    const MeshSdf& sdf() const { return sdf_; }

    /** @brief Remove all particles. */
    void clear();

    /**
     * @brief Spawn a roughly spherical drop of particles near the surface.
     * @param center world position of the drop center.
     * @param radius drop radius in world units.
     * @param count number of particles to add (clamped to capacity).
     * @return number of particles actually added.
     */
    int spawnDrop(const glm::vec3& center, float radius, int count);

    /** @brief Advance the simulation by dt seconds (params.iterations substeps). */
    void step(float dt);
    /**
     * @brief Advance using an explicit shared-backend substep count.
     * @param dt Non-negative simulation duration in seconds.
     * @param substeps Positive integration substeps supplied by the backend contract.
     */
    void step(float dt, int substeps);

    /** @return number of live particles. */
    int particleCount() const { return count_; }

    /** @return capacity of the particle buffer. */
    int maxParticles() const { return maxParticles_; }

    /** @return live particles (positions + velocities). */
    const std::vector<FluidParticle>& particles() const { return particles_; }

    /** @return live particles for mutation (GPU readback mirror). */
    std::vector<FluidParticle>& particles() { return particles_; }

    /** @return per-particle SPH densities (updated each substep). */
    const std::vector<float>& densities() const { return densities_; }

    /** @return per-particle densities for mutation (GPU readback mirror). */
    std::vector<float>& densities() { return densities_; }

    /** @return tuning parameters (mutable). */
    FluidParams& params() { return params_; }

    /** @return tuning parameters (read-only). */
    const FluidParams& params() const { return params_; }

private:
    void rebuildGrid();
    void computeDensitiesAndGrads();
    void computeLambdas();
    void applyPositionCorrections();
    void integrate(float dt);

    FluidParams                params_;
    MeshSdf                    sdf_;
    SimGrid                    grid_;
    int                        maxParticles_ = 0;
    int                        count_        = 0;
    std::vector<FluidParticle> particles_;
    std::vector<float>         densities_;
    std::vector<float>         lambdas_;
    std::vector<glm::vec3>     gradSums_;
    std::vector<int>           cellHead_;
    std::vector<int>           cellNext_;
};

}  // namespace eve::fluids
