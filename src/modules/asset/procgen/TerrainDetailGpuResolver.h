#pragma once

/** @file TerrainDetailGpuResolver.h @brief Default GPU resolver for Unity terrain Detail resources. */

#include "asset/procgen/TerrainVegetationGpu.h"

#include <memory>

namespace eve::asset {
class EvpackResourceReader;
struct EvpackCapabilities;
}

namespace eve::graphics {
class Graphics;
}

namespace eve::asset_procgen {

/**
 * @brief Owning lazy GPU resource resolver for texture-backed terrain Detail prototypes.
 * @ownership Uploaded meshes and images are owned by the bound Graphics backend and leased by this resolver.
 * @lifetime The reader and Graphics backend must outlive this object and its explicit release.
 * @thread Construction, resolve, release, and destruction are graphics-thread affine.
 * @reentrancy Calls no scripts or caller callbacks and must not be entered recursively.
 */
class TerrainDetailGpuResolver final : public ITerrainVegetationGpuResolver {
public:
    TerrainDetailGpuResolver(const asset::EvpackResourceReader& reader,
                             const asset::EvpackCapabilities& capabilities, graphics::Graphics& graphics);
    ~TerrainDetailGpuResolver() override;
    TerrainDetailGpuResolver(const TerrainDetailGpuResolver&)            = delete;
    TerrainDetailGpuResolver& operator=(const TerrainDetailGpuResolver&) = delete;

    /** @brief Load and cache one Detail card, image, and masked material transactionally. */
    [[nodiscard]] Result<TerrainVegetationGpuPrototype> resolve(
        const TerrainVegetationGpuPrototypeRequest& prototype) override;

    /** @brief Draw one realized Detail instance in the active cascade shadow pass. */
    [[nodiscard]] Result<void> drawShadow(
        const TerrainVegetationGpuPrototypeRequest& prototype,
        const std::array<float, 16>& transform,
        const std::array<float, 16>& lightViewProjection) override;

    /** @brief Release all backend leases; failed releases remain owned for retry. */
    [[nodiscard]] Result<void> release();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Owning runtime for one cooked Unity Terrain Detail instance set.
 * @ownership Owns the realization, GPU resolver leases, and render registration.
 * @lifetime The reader and Graphics backend passed to load must outlive this object and release.
 * @thread Load, frame updates, rendering callbacks, release, and destruction are graphics-thread affine.
 * @reentrancy Calls no scripts; release must not run from a render callback.
 */
class TerrainDetailRuntime final {
public:
    /**
     * @brief Load, realize, and register one Detail set atomically; GPU resources resolve on first render.
     * @param reader Borrowed EVPACK reader containing the instance set and its resource closure.
     * @param capabilities Runtime variant used for every resource selection.
     * @param graphics Borrowed graphics backend that owns uploaded resources.
     * @param asset Canonical `eve.instance-set` asset reference.
     * @param limits Allocation limits applied before realization publication.
     * @return Owning active runtime, or a structured load failure with no render registration.
     */
    [[nodiscard]] static Result<std::unique_ptr<TerrainDetailRuntime>> load(
        const asset::EvpackResourceReader& reader, const asset::EvpackCapabilities& capabilities,
        graphics::Graphics& graphics, const AssetRef& asset, const TerrainVegetationLimits& limits = {});

    ~TerrainDetailRuntime();
    TerrainDetailRuntime(const TerrainDetailRuntime&)            = delete;
    TerrainDetailRuntime& operator=(const TerrainDetailRuntime&) = delete;

    /** @brief Update the absolute simulation time used for deterministic grass motion. */
    [[nodiscard]] Result<void> setFrame(TerrainVegetationGpuFrame frame);

    /**
     * @brief Return the latest asynchronous render-submission diagnostic, if any.
     * @ownership Borrowed; the runtime retains ownership.
     * @lifetime Valid until the next render callback, release, or destruction.
     */
    [[nodiscard]] const Diagnostic* lastSubmissionError() const;

    /** @brief Unregister rendering and release all backend resources; safe to retry after failure. */
    [[nodiscard]] Result<void> release();

private:
    TerrainDetailRuntime(const asset::EvpackResourceReader& reader,
                         const asset::EvpackCapabilities& capabilities, graphics::Graphics& graphics);

    std::unique_ptr<TerrainDetailGpuResolver> resolver_;
    std::unique_ptr<TerrainVegetationRenderer> renderer_;
};

}  // namespace eve::asset_procgen
