#pragma once

/**
 * @file SurfaceFluidContracts.h
 * @brief Domain contracts separating surface simulation, constraints, and SSF presentation.
 *
 * The interfaces contain no graphics or gpgpu include. Existing CPU/GPU
 * solvers remain behind small adapters, so the boundary can be tested in a
 * headless profile and a renderer can be replaced independently.
 */

#include "common/Result.h"
#include "common/Time.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace eve::fluids {

class FluidSurfaceRenderer;
struct FluidSurfaceParams;

/** @brief Backend-neutral contract for advancing a surface-fluid solver. */
class ISurfaceFluidSimulation {
public:
    /** @brief Releases the borrowed-solver adapter. */
    virtual ~ISurfaceFluidSimulation() = default;

    /**
     * @brief Advances the solver at an injected simulation tick.
     * @param step Tick and fixed duration; no wall clock is consulted.
     * @return Applied, or a structured rejection/failure.
     */
    [[nodiscard("check the surface-fluid step outcome")]]
    virtual eve::Result<void> step(const eve::SimulationStep& step) = 0;

    /** @brief Returns the number of live particles. */
    [[nodiscard]] virtual int particleCount() const noexcept = 0;
    /** @brief Borrows a read-only live-particle view until the next solver mutation. */
    [[nodiscard]] virtual std::span<const FluidParticle> particles() const noexcept = 0;
};

/** @brief Backend-neutral contract for mesh-surface binding and traversal. */
class ISurfaceConstraint {
public:
    /** @brief Releases the borrowed-constraint adapter. */
    virtual ~ISurfaceConstraint() = default;

    /** @brief Builds topology and pose state from owning caller data. */
    [[nodiscard("check surface constraint construction")]]
    virtual eve::Result<void> build(const std::vector<glm::vec3>& positions, const std::vector<std::uint32_t>& indices,
                                    const std::vector<glm::vec2>& uvs = {}) = 0;

    /** @brief Applies a rigid pose while preserving the previous pose. */
    [[nodiscard("check surface constraint pose update")]]
    virtual eve::Result<void> setTransform(const glm::mat4& transform) = 0;

    /** @brief Applies a deformed pose with the existing topology. */
    [[nodiscard("check surface constraint pose update")]]
    virtual eve::Result<void> setDeformedPositions(const std::vector<glm::vec3>& worldPositions) = 0;

    /** @brief Returns whether topology and poses are currently valid. */
    [[nodiscard]] virtual bool isValid() const noexcept = 0;

    /** @brief Evaluates a material-space location in the current pose. */
    [[nodiscard("check surface location evaluation")]]
    virtual eve::Result<SurfaceSample> evaluate(const SurfaceLocation& location, eve::Duration poseDelta) const = 0;

    /** @brief Projects a world point to the nearest material-space location. */
    [[nodiscard("check surface projection")]]
    virtual eve::Result<SurfaceLocation> project(const glm::vec3& worldPosition, float maxDistance) const = 0;

    /** @brief Walks a material-space location over triangle adjacency. */
    [[nodiscard("check surface traversal")]]
    virtual eve::Result<SurfaceWalkResult> walk(const SurfaceLocation& start, const glm::vec3& displacement,
                                                int maxCrossings = 16) const = 0;
};

/** @brief Presentation contract for screen-space surface reconstruction. */
class IScreenSpaceSurfaceReconstruction {
public:
    /** @brief Releases the renderer adapter and its presentation buffers. */
    virtual ~IScreenSpaceSurfaceReconstruction() = default;

    /**
     * @brief Reconstructs depth, thickness, normals, and color from particles.
     * @return Applied, or a structured input/device failure.
     */
    [[nodiscard("check screen-space reconstruction")]]
    virtual eve::Result<void> reconstruct(std::span<const glm::vec3> positions, float particleRadius) = 0;

    /** @brief Output width in pixels. */
    [[nodiscard]] virtual int width() const noexcept = 0;
    /** @brief Output height in pixels. */
    [[nodiscard]] virtual int height() const noexcept = 0;
    /** @brief Linear view depth per pixel. */
    [[nodiscard]] virtual std::span<const float> depth() const noexcept = 0;
    /** @brief Reconstructed view-space normal per pixel. */
    [[nodiscard]] virtual std::span<const glm::vec3> normals() const noexcept = 0;
    /** @brief Accumulated thickness per pixel. */
    [[nodiscard]] virtual std::span<const float> thickness() const noexcept = 0;
    /** @brief RGBA8 presentation color per pixel. */
    [[nodiscard]] virtual std::span<const std::uint8_t> color() const noexcept = 0;
    /** @brief Whether the optional GPU path is active. */
    [[nodiscard]] virtual bool usingGpu() const noexcept = 0;
};

/** @brief Adapter exposing the existing CPU FluidSimulation through the contract. */
class FluidSimulationAdapter final : public ISurfaceFluidSimulation {
public:
    /** @param solver Borrowed CPU solver; it must outlive this adapter. */
    explicit FluidSimulationAdapter(FluidSimulation& solver) noexcept : solver_(&solver) {}

    [[nodiscard]] eve::Result<void>              step(const eve::SimulationStep& step) override;
    [[nodiscard]] int                            particleCount() const noexcept override;
    [[nodiscard]] std::span<const FluidParticle> particles() const noexcept override;

private:
    FluidSimulation*    solver_   = nullptr;
    eve::SimulationTick lastTick_ = eve::SimulationTick::zero();
};

/** @brief Adapter exposing existing FluidSurfaceBinding operations as checked results. */
class FluidSurfaceConstraintAdapter final : public ISurfaceConstraint {
public:
    /** @param binding Borrowed surface binding; it must outlive this adapter. */
    explicit FluidSurfaceConstraintAdapter(FluidSurfaceBinding& binding) noexcept : binding_(&binding) {}

    [[nodiscard]] eve::Result<void> build(const std::vector<glm::vec3>&     positions,
                                          const std::vector<std::uint32_t>& indices,
                                          const std::vector<glm::vec2>&     uvs = {}) override;
    [[nodiscard]] eve::Result<void> setTransform(const glm::mat4& transform) override;
    [[nodiscard]] eve::Result<void> setDeformedPositions(const std::vector<glm::vec3>& worldPositions) override;
    [[nodiscard]] bool              isValid() const noexcept override;
    [[nodiscard]] eve::Result<SurfaceSample>     evaluate(const SurfaceLocation& location,
                                                          eve::Duration          poseDelta) const override;
    [[nodiscard]] eve::Result<SurfaceLocation>   project(const glm::vec3& worldPosition,
                                                         float            maxDistance) const override;
    [[nodiscard]] eve::Result<SurfaceWalkResult> walk(const SurfaceLocation& start, const glm::vec3& displacement,
                                                      int maxCrossings = 16) const override;

private:
    FluidSurfaceBinding* binding_ = nullptr;
};

/**
 * @brief Minimal SSF adapter over the existing CPU/GPU FluidSurfaceRenderer.
 *
 * The renderer remains a presentation implementation; callers depend only on
 * this interface and may request the GPU path without making it mandatory.
 */
class ScreenSpaceSurfaceReconstructionAdapter final : public IScreenSpaceSurfaceReconstruction {
public:
    /**
     * @param params Camera and output configuration.
     * @param preferGpu Requests the optional compute path.
     * @throws eve::Exception for non-positive output dimensions.
     */
    ScreenSpaceSurfaceReconstructionAdapter(const FluidSurfaceParams& params, bool preferGpu);
    ~ScreenSpaceSurfaceReconstructionAdapter() override;

    ScreenSpaceSurfaceReconstructionAdapter(const ScreenSpaceSurfaceReconstructionAdapter&)            = delete;
    ScreenSpaceSurfaceReconstructionAdapter& operator=(const ScreenSpaceSurfaceReconstructionAdapter&) = delete;

    [[nodiscard]] eve::Result<void> reconstruct(std::span<const glm::vec3> positions, float particleRadius) override;
    [[nodiscard]] int               width() const noexcept override;
    [[nodiscard]] int               height() const noexcept override;
    [[nodiscard]] std::span<const float>        depth() const noexcept override;
    [[nodiscard]] std::span<const glm::vec3>    normals() const noexcept override;
    [[nodiscard]] std::span<const float>        thickness() const noexcept override;
    [[nodiscard]] std::span<const std::uint8_t> color() const noexcept override;
    [[nodiscard]] bool                          usingGpu() const noexcept override;

private:
    std::unique_ptr<FluidSurfaceRenderer> renderer_;
};

}  // namespace eve::fluids
