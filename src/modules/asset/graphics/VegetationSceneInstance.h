#pragma once

/** @file VegetationSceneInstance.h @brief Automatic Scene Template to TVE manager runtime association. */

#include "asset/graphics/VegetationSceneLiveElements.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"

namespace eve::asset_graphics {

/** @brief Owning decoded result of one unique source-GUID scene-manager association. */
struct VegetationSceneAssociation {
    asset_scene::LoadedSceneTemplate scene;
    LoadedVegetationScene manager;
    std::map<std::string, graphics::VegetationMask> masks;
};

/** @brief Resolve a Scene Template to exactly one manager asset in the same admitted EVPACK. */
class EVENGINE_API_WORLD VegetationSceneAssociationLoader {
public:
    /** @brief Bind a borrowed immutable reader which must outlive this loader. */
    explicit VegetationSceneAssociationLoader(const asset::EvpackResourceReader& reader) noexcept : reader_(reader) {}

    /**
     * @brief Load the template, find the unique manager with the same Unity source GUID, and decode all Element masks.
     * @param sceneTemplate Stable Scene Template asset identity.
     * @param capabilities Runtime variant capabilities applied consistently to every candidate.
     * @return Owning association; missing and duplicate matches fail without partial output.
     * @thread Worker-safe while the reader is read concurrently.
     */
    [[nodiscard]] Result<VegetationSceneAssociation> load(
        const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities) const;

private:
    const asset::EvpackResourceReader& reader_;
};

/**
 * @brief Owning executable instance coupling one Scene Template, its manager fields and live Element registry.
 * GPU resources are graphics-thread affine. Base material texture pointers remain borrowed and must outlive this instance.
 */
class EVENGINE_API_WORLD VegetationSceneInstance final {
public:
    /**
     * @brief Resolve and publish the manager automatically from a Scene Template asset.
     * @param reader Borrowed only during creation; no package views survive.
     * @param factory Borrowed graphics factory which must outlive the returned instance and explicit release.
     * @param sceneTemplate Scene Template whose source GUID selects the manager.
     * @param capabilities Runtime variant capabilities.
     * @param baseSurface Owning parameter snapshot; referenced textures remain borrowed.
     * @param baseMotion Detached explicit-time motion state.
     * @param build Owning atlas values plus call-scoped raster spans and initial nonzero GPU revision.
     * @param owner Nonzero live-Element registry identity.
     * @return Complete owning instance, or failure after candidate GPU cleanup.
     * @thread Graphics thread only; synchronous and non-reentrant.
     */
    [[nodiscard]] static Result<std::unique_ptr<VegetationSceneInstance>> create(
        const asset::EvpackResourceReader& reader, graphics::IResourceFactory& factory,
        const AssetRef& sceneTemplate, const asset::EvpackCapabilities& capabilities,
        graphics::PbrSurface baseSurface, graphics::VegetationMotion baseMotion,
        const VegetationSceneGpuBuild& build, std::uint64_t owner);

    /** @brief Borrow the associated declarative scene for this instance lifetime. */
    [[nodiscard]] const asset_scene::LoadedSceneTemplate& scene() const noexcept { return association_.scene; }
    /** @brief Borrow the currently bound manager material snapshot. */
    [[nodiscard]] const graphics::PbrSurface& surface() const noexcept { return runtime_->surface(); }
    /** @brief Borrow the dynamic Element registry. */
    [[nodiscard]] const VegetationSceneLiveElements& liveElements() const noexcept { return *liveElements_; }

    /** @brief Register one live Element and republish this associated instance atomically. */
    [[nodiscard]] Result<VegetationSceneElementPublication> registerElement(
        std::uint64_t expectedRevision, const VegetationSceneElement& element,
        const VegetationSceneGpuBuild& build);
    /** @brief Withdraw one live Element and republish this associated instance atomically. */
    [[nodiscard]] Result<VegetationSceneElementPublication> withdrawElement(
        std::uint64_t expectedRevision, VegetationSceneElementHandle handle,
        const VegetationSceneGpuBuild& build);
    /** @brief Invalidate surface borrows and release manager GPU resources. */
    [[nodiscard]] Result<void> release() { return runtime_->release(); }

private:
    VegetationSceneInstance(VegetationSceneAssociation association, graphics::PbrSurface baseSurface,
                            graphics::VegetationMotion baseMotion,
                            std::unique_ptr<VegetationSceneGpuRuntime> runtime,
                            std::unique_ptr<VegetationSceneLiveElements> liveElements) noexcept
        : association_(std::move(association)), baseSurface_(std::move(baseSurface)),
          baseMotion_(std::move(baseMotion)), runtime_(std::move(runtime)),
          liveElements_(std::move(liveElements)) {}

    VegetationSceneAssociation association_;
    graphics::PbrSurface baseSurface_;
    graphics::VegetationMotion baseMotion_;
    std::unique_ptr<VegetationSceneGpuRuntime> runtime_;
    std::unique_ptr<VegetationSceneLiveElements> liveElements_;
};

}  // namespace eve::asset_graphics
