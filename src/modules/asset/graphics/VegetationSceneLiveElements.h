#pragma once

/** @file VegetationSceneLiveElements.h @brief Transactional runtime registration of TVE Element volumes. */

#include "asset/graphics/EvpackVegetationScene.h"
#include "common/RuntimeHandle.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace eve::asset_graphics {

struct VegetationSceneElementHandleTag;

/** @brief Instance-qualified handle for one dynamically registered TVE Element volume. */
class VegetationSceneElementHandle {
public:
    using LocalHandle = RuntimeHandle<VegetationSceneElementHandleTag>;
    using index_type = LocalHandle::index_type;
    using generation_type = LocalHandle::generation_type;

    constexpr VegetationSceneElementHandle() noexcept = default;
    constexpr VegetationSceneElementHandle(std::uint64_t owner, index_type index,
                                           generation_type generation) noexcept
        : owner_(owner), local_(index, generation) {}
    [[nodiscard]] constexpr bool isValid() const noexcept { return owner_ != 0 && local_.isValid(); }
    [[nodiscard]] constexpr std::uint64_t owner() const noexcept { return owner_; }
    [[nodiscard]] constexpr index_type index() const noexcept { return local_.index(); }
    [[nodiscard]] constexpr generation_type generation() const noexcept { return local_.generation(); }
    friend constexpr bool operator==(const VegetationSceneElementHandle&,
                                     const VegetationSceneElementHandle&) noexcept = default;

private:
    std::uint64_t owner_ = 0;
    LocalHandle local_{};
};

/** @brief Receipt for one published dynamic Element registration or withdrawal. */
struct VegetationSceneElementPublication {
    VegetationSceneElementHandle handle;
    std::uint64_t registryRevision = 0;
    VegetationSceneGpuPublication gpu;
};

/**
 * @brief Owning registry that atomically couples dynamic Element state to a scene GPU runtime.
 * Imported Elements remain immutable. Dynamic slots use generation-qualified handles and stable slot order.
 * The registry and GPU runtime are graphics-thread affine; no callback is invoked while state is changing.
 */
class VegetationSceneLiveElements final {
public:
    /**
     * @brief Create a registry from one validated imported scene.
     * @param baseScene Detached owning scene whose imported Elements remain the stable prefix.
     * @param owner Nonzero instance identity used to reject foreign handles.
     * @return Owning registry, or InvalidArgument/Failed without publication.
     */
    [[nodiscard]] static Result<std::unique_ptr<VegetationSceneLiveElements>> create(
        LoadedVegetationScene baseScene, std::uint64_t owner);

    /** @brief Current successful mutation revision; begins at one. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Detached scene containing imported Elements followed by live slots in stable index order. */
    [[nodiscard]] Result<LoadedVegetationScene> snapshot() const;

    /**
     * @brief Register and publish one Element as an atomic registry/GPU transaction.
     * @param expectedRevision Registry revision observed by the caller.
     * @param expectedGpuRevision GPU runtime revision observed by the caller.
     * @param element Detached Element candidate; copied before GPU work and owned after success.
     * @return New handle and revisions, or failure with registry and GPU runtime unchanged.
     * @thread Graphics thread only; synchronous and non-reentrant.
     */
    [[nodiscard]] Result<VegetationSceneElementPublication> registerElement(
        std::uint64_t expectedRevision, std::uint64_t expectedGpuRevision, const VegetationSceneElement& element,
        VegetationSceneGpuRuntime& runtime, const graphics::PbrSurface& baseSurface,
        const graphics::VegetationMotion& baseMotion,
        const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build);

    /**
     * @brief Withdraw and publish one live Element as an atomic registry/GPU transaction.
     * Foreign, stale and already withdrawn handles are rejected without mutation.
     * @thread Graphics thread only; synchronous and non-reentrant.
     */
    [[nodiscard]] Result<VegetationSceneElementPublication> withdrawElement(
        std::uint64_t expectedRevision, std::uint64_t expectedGpuRevision, VegetationSceneElementHandle handle,
        VegetationSceneGpuRuntime& runtime, const graphics::PbrSurface& baseSurface,
        const graphics::VegetationMotion& baseMotion,
        const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build);

private:
    struct Slot {
        std::optional<VegetationSceneElement> element;
        VegetationSceneElementHandle::generation_type generation = 1;
        bool retired = false;
    };

    VegetationSceneLiveElements(LoadedVegetationScene baseScene, std::uint64_t owner) noexcept
        : baseScene_(std::move(baseScene)), owner_(owner) {}

    LoadedVegetationScene baseScene_;
    std::vector<Slot> slots_;
    std::uint64_t owner_ = 0;
    std::uint64_t revision_ = 1;
};

}  // namespace eve::asset_graphics
