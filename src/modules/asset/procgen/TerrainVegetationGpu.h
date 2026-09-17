#pragma once

/** @file TerrainVegetationGpu.h @brief GPU-driven adapter for realized terrain vegetation. */

#include "asset/procgen/TerrainVegetationRuntime.h"
#include "graphics/GpuDrivenTypes.h"

#include <memory>

namespace eve::graphics {
class Graphics;
}

namespace eve::asset_procgen {

/** @brief GPU table slots resolved for one vegetation prototype. */
struct TerrainVegetationGpuPrototype {
    std::uint32_t meshId     = graphics::kInvalidGpuDrivenSlot;
    std::uint32_t materialId = graphics::kInvalidGpuDrivenSlot;
    std::uint32_t flags      = 0x7u;
    std::uint32_t lodGroupId = graphics::kInvalidGpuDrivenSlot;
    struct Part {
        std::array<float, 16> localTransform{};
        std::uint32_t meshId     = graphics::kInvalidGpuDrivenSlot;
        std::uint32_t materialId = graphics::kInvalidGpuDrivenSlot;
        std::uint32_t flags      = 0x7u;
        std::uint32_t lodGroupId = graphics::kInvalidGpuDrivenSlot;
    };
    /** Optional prefab parts; empty means the four root slots describe one draw. */
    std::vector<Part> parts;
};

/** @brief One owning realization's prototype request passed to the graphics resource provider. */
struct TerrainVegetationGpuPrototypeRequest {
    std::string_view                prototype;
    const RuntimeInstancePrototype* detail = nullptr;
};

/** @brief Provider boundary from stable prototype names to backend table slots. */
class ITerrainVegetationGpuResolver {
public:
    virtual ~ITerrainVegetationGpuResolver() = default;

    /**
     * @brief Resolve one prototype and optional terrain Detail metadata without mutating the realization.
     * @return Valid mesh/material slots retained through the following submit call.
     * @thread Called on the graphics thread.
     * @reentrancy The implementation must not call terrain vegetation submission recursively.
     * @lifetime The request and optional detail pointer are borrowed only for this call and must not be retained.
     */
    [[nodiscard]] virtual Result<TerrainVegetationGpuPrototype> resolve(
        const TerrainVegetationGpuPrototypeRequest& prototype) = 0;

    /**
     * @brief Submit one realized instance inside an active cascade shadow pass.
     * @param prototype Borrowed prototype identity and optional Detail metadata.
     * @param transform Finite column-major world transform for the instance.
     * @param lightViewProjection Finite column-major cascade light view-projection matrix.
     * @return Success after all eligible prototype parts are submitted, or a structured resource error.
     * @thread Graphics thread only; synchronous and non-reentrant.
     * @lifetime Arguments are borrowed only for this call and must not be retained.
     */
    [[nodiscard]] virtual Result<void> drawShadow(
        const TerrainVegetationGpuPrototypeRequest& prototype,
        const std::array<float, 16>& transform,
        const std::array<float, 16>& lightViewProjection) = 0;
};

/** @brief Detached CPU upload plan accepted by both Vulkan and WebGPU GPU-driven paths. */
struct TerrainVegetationGpuPlan {
    std::vector<graphics::GpuInstance> instances;
};

/** @brief Explicit frame inputs for deterministic terrain Detail animation. */
struct TerrainVegetationGpuFrame {
    double timeSeconds = 0.0;
};

/**
 * @brief Resolve every prototype and build GPU records transactionally.
 * @param realization Borrowed immutable prototype-sorted realization.
 * @param resolver Borrowed provider invoked once per bucket.
 * @return Detached GPU records, or failure before any graphics submission.
 * @thread Graphics thread only because resolver resources are backend-owned.
 */
[[nodiscard]] Result<TerrainVegetationGpuPlan> buildTerrainVegetationGpuPlan(
    const TerrainVegetationRealization& realization, ITerrainVegetationGpuResolver& resolver,
    const TerrainVegetationGpuFrame& frame = {});

/**
 * @brief Build and submit one terrain vegetation realization to the current GPU-driven frame.
 * @return Applied status after submission, or a structured failure without a partial submit.
 * @thread Graphics thread only; synchronous and non-reentrant.
 */
[[nodiscard]] Result<void> submitTerrainVegetation(graphics::Graphics&                 graphics,
                                                   const TerrainVegetationRealization& realization,
                                                   ITerrainVegetationGpuResolver&      resolver,
                                                   const TerrainVegetationGpuFrame&    frame = {});

/**
 * @brief Own one realized terrain Detail set and submit it from RenderSystem3D's forward phase.
 *
 * The resolver is borrowed and must outlive this object. Construction and destruction are
 * graphics-thread operations. The realization is owned so import/hot-reload code can atomically
 * replace the renderer without retaining temporary loader storage.
 */
class TerrainVegetationRenderer {
public:
    TerrainVegetationRenderer(TerrainVegetationRealization realization,
                              ITerrainVegetationGpuResolver& resolver);
    ~TerrainVegetationRenderer();
    TerrainVegetationRenderer(const TerrainVegetationRenderer&) = delete;
    TerrainVegetationRenderer& operator=(const TerrainVegetationRenderer&) = delete;
    TerrainVegetationRenderer(TerrainVegetationRenderer&&) = delete;
    TerrainVegetationRenderer& operator=(TerrainVegetationRenderer&&) = delete;

    /** @brief Replace the explicit animation clock used by the next render frame. */
    [[nodiscard]] Result<void> setFrame(TerrainVegetationGpuFrame frame);
    /**
     * @brief Return the most recent asynchronous submission failure, or null when healthy.
     * @return Borrowed diagnostic owned by this renderer.
     * @lifetime Valid until the next forward-pass submission or renderer destruction; do not retain it.
     * @thread Graphics thread only.
     */
    [[nodiscard]] const Diagnostic* lastSubmissionError() const;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::uint64_t          drawerToken_ = 0;
    std::uint64_t          shadowDrawerToken_ = 0;
};

}  // namespace eve::asset_procgen
