#pragma once

/**
 * @brief Fluids module — interactive surface fluid simulation.
 *
 * Particles are constrained to mesh SDFs, flow down the surface under
 * tangential gravity, and (later phases) form droplets through cohesion /
 * surface tension. The solver runs on the GPU through the gpgpu module with a
 * CPU reference fallback; the screen-space surface reconstruction pipeline
 * consumes the same particle buffers.
 */

#include "common/Module.h"
#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceRenderer.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace eve::gpgpu {
class ComputeShader;
class Gpgpu;
class GpuBuffer;
class Sequence;
}  // namespace eve::gpgpu

namespace eve::fluids {

/** @brief GPU-backed surface fluid simulator (falls back to the CPU solver). */
class FluidSimulator {
public:
    /**
     * @param maxParticles particle buffer capacity.
     * @param params initial tuning parameters.
     * @param preferGpu use the gpgpu compute path when a device is available.
     */
    FluidSimulator(int maxParticles, const FluidParams& params, bool preferGpu);
    ~FluidSimulator();

    FluidSimulator(const FluidSimulator&)            = delete;
    FluidSimulator& operator=(const FluidSimulator&) = delete;

    /** @brief Replace the collision surface (re-uploads the SDF on the GPU path). */
    void setSdf(const MeshSdf& sdf);

    /** @return the currently bound SDF. */
    const MeshSdf& sdf() const { return sim_.sdf(); }

    /**
     * @brief Spawn a drop of particles near the surface.
     * @param center drop center.
     * @param radius drop radius.
     * @param count particle count.
     * @return particles actually added.
     */
    int spawnDrop(const glm::vec3& center, float radius, int count);

    /** @brief Advance the simulation by dt seconds. */
    void step(float dt);

    /** @return live particle count. */
    int getParticleCount() const { return sim_.particleCount(); }

    /** @return particle buffer capacity. */
    int getMaxParticles() const { return sim_.maxParticles(); }

    /** @return true when the GPU compute path is active. */
    bool usingGpu() const { return gpuOk_; }

    /** @brief Copy live particle positions out (CPU mirror; GPU path downloads). */
    void readPositions(std::vector<glm::vec3>& out) const;

    /** @brief Copy per-particle densities out (CPU mirror). */
    void readDensities(std::vector<float>& out) const;

    /** @return mutable tuning parameters. */
    FluidParams& params() { return sim_.params(); }

    /** @return tuning parameters (read-only). */
    const FluidParams& params() const { return sim_.params(); }

    /** @brief Set the gravity vector. */
    void setGravity(float x, float y, float z);

    /** @brief Set XSPH viscosity strength. */
    void setViscosity(float viscosity);

    /** @brief Set Akinci-style fluid-fluid cohesion strength. */
    void setCohesion(float cohesion);

    /** @brief Set fluid-surface adhesion strength. */
    void setAdhesion(float adhesion);

    /** @brief Set PBF density-constraint passes per substep. */
    void setPbfIterations(int passes);

    /** @brief Set linear air damping. */
    void setDamping(float damping);

    /** @brief Set particle radius (support radius is unchanged). */
    void setParticleRadius(float radius);

    /** @brief Set SPH support radius. */
    void setSupportRadius(float h);

    /**
     * @brief Convenience: bake a sphere SDF around the origin.
     * @param cx sphere center X.
     * @param cy sphere center Y.
     * @param cz sphere center Z.
     * @param radius sphere radius.
     * @param res voxel resolution per axis.
     */
    void setSdfSphere(float cx, float cy, float cz, float radius, int res);

private:
    bool ensureGpu();
    void uploadSdf();
    void uploadParticles();
    void downloadParticles();
    void setCommonConstants(gpgpu::ComputeShader* shader, float dt);

    FluidSimulation sim_;
    bool            preferGpu_ = false;
    bool            gpuOk_     = false;
    bool            sdfDirty_  = true;

    eve::gpgpu::Gpgpu*         gpgpu_           = nullptr;
    eve::gpgpu::ComputeShader* shClear_         = nullptr;
    eve::gpgpu::ComputeShader* shBuild_         = nullptr;
    eve::gpgpu::ComputeShader* shDensityLambda_ = nullptr;
    eve::gpgpu::ComputeShader* shDelta_         = nullptr;
    eve::gpgpu::ComputeShader* shApply_         = nullptr;
    eve::gpgpu::ComputeShader* shIntegrate_     = nullptr;
    eve::gpgpu::GpuBuffer*     bufPos_          = nullptr;
    eve::gpgpu::GpuBuffer*     bufVel_          = nullptr;
    eve::gpgpu::GpuBuffer*     bufHead_         = nullptr;
    eve::gpgpu::GpuBuffer*     bufNext_         = nullptr;
    eve::gpgpu::GpuBuffer*     bufDens_         = nullptr;
    eve::gpgpu::GpuBuffer*     bufLambda_       = nullptr;
    eve::gpgpu::GpuBuffer*     bufGrad_         = nullptr;
    eve::gpgpu::GpuBuffer*     bufSdf_          = nullptr;
    eve::gpgpu::GpuBuffer*     stagePos_        = nullptr;
    eve::gpgpu::GpuBuffer*     stageVel_        = nullptr;
    eve::gpgpu::GpuBuffer*     stageDens_       = nullptr;
    eve::gpgpu::Sequence*      seq_             = nullptr;
    SimGrid                    grid_;
};

/** @brief Fluids module — factory + script binding. */
class Fluids : public Module {
public:
    Module_REG(Fluids);
    Fluids();
    ~Fluids() override;

    /**
     * @brief Create a surface fluid simulator.
     * @param maxParticles particle buffer capacity.
     * @return simulator owned by the module.
     */
    FluidSimulator* newSimulator(int maxParticles = 8192);

    /**
     * @brief Create a screen-space fluid renderer.
     * @param width target width in pixels.
     * @param height target height in pixels.
     * @return renderer owned by the module.
     */
    FluidSurfaceRenderer* newSurfaceRenderer(int width = 160, int height = 160);

    /** @return number of live simulators owned by the module. */
    int getSimulatorCount() const;

    /** @return number of live renderers owned by the module. */
    int getRendererCount() const;

private:
    std::vector<std::unique_ptr<FluidSimulator>>       simulators_;
    std::vector<std::unique_ptr<FluidSurfaceRenderer>> renderers_;
};

}  // namespace eve::fluids
