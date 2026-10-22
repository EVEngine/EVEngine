#pragma once
#include "common/Export.h"


#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "common/PrefabRootPose.h"
#include "common/ResourceRef.h"

namespace eve::asset {
class EvpackResourceReader;
struct EvpackCapabilities;
}  // namespace eve::asset
namespace eve::graphics {
class Graphics;
class IMeshResourceFactory;
class IImageResourceFactory;
class Mesh;
class Material;
}  // namespace eve::graphics

namespace eve::asset_graphics {
class EvpackStaticPrefab;
/** @brief Owning resource pin for an admitted immutable prefab version; does not allocate scene materials.
 * @ownership Retains the prefab and blocks its explicit GPU release until this pin is destroyed.
 * @lifetime Resource factories must outlive this pin and the retained prefab.
 * @thread Graphics-owner affine, synchronous, no scripts or locks.
 */
class EVENGINE_API_WORLD StaticPrefabResourcePin final {
public:
    /** @brief Drop the pin; the final prefab owner may release GPU resources.
     * @cost Fixed counter work; final-owner backend cleanup is amortized per loaded prefab version.
     */
    ~StaticPrefabResourcePin();
    StaticPrefabResourcePin(const StaticPrefabResourcePin&)            = delete;
    StaticPrefabResourcePin& operator=(const StaticPrefabResourcePin&) = delete;
    StaticPrefabResourcePin(StaticPrefabResourcePin&&)                 = delete;
    StaticPrefabResourcePin& operator=(StaticPrefabResourcePin&&)      = delete;

private:
    friend class EvpackStaticPrefab;
    explicit StaticPrefabResourcePin(std::shared_ptr<EvpackStaticPrefab> owner);
    std::shared_ptr<EvpackStaticPrefab> owner_;
};
/** @brief Renderer inputs borrowed from PreparedStaticPrefabScene until its release/destruction. */
struct StaticPrefabScenePart {
    std::array<float, 16> localTransform{};
    graphics::Mesh*       mesh        = nullptr;
    graphics::Material*   material    = nullptr;
    bool                  castShadows = true, receiveShadows = true;
};
/** @brief Owning preparation lease pinning prefab GPU resources and complete scene materials.
 * @thread Graphics-owner affine, synchronous, non-reentrant; no scripts or locks.
 * @lifetime Graphics and the prefab's resource factories must outlive this lease. Unlink renderables before release.
 */
class EVENGINE_API_WORLD PreparedStaticPrefabScene final {
public:
    /** @brief Release material records and the prefab pin; reports cleanup errors to stderr on destruction. */
    ~PreparedStaticPrefabScene();
    PreparedStaticPrefabScene(const PreparedStaticPrefabScene&)            = delete;
    PreparedStaticPrefabScene& operator=(const PreparedStaticPrefabScene&) = delete;
    PreparedStaticPrefabScene(PreparedStaticPrefabScene&&)                 = delete;
    PreparedStaticPrefabScene& operator=(PreparedStaticPrefabScene&&)      = delete;
    /** @brief Borrow prepared renderer parts until release/destruction; no callbacks or per-frame lookup. */
    [[nodiscard]] const std::vector<StaticPrefabScenePart>& parts() const noexcept;
    /** @brief Release after all borrowed parts are unlinked; failed material cleanup retains state for retry.
     * @return Success or backend cleanup failure. Repeated successful release is harmless.
     */
    [[nodiscard]] Result<void> release();

private:
    friend class EvpackStaticPrefab;
    struct Impl;
    explicit PreparedStaticPrefabScene(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief One prefab renderer registered in backend GPU-driven tables. */
struct StaticPrefabGpuPart {
    std::array<float, 16> localTransform{};
    std::uint32_t meshId = 0xffffffffu, materialId = 0xffffffffu, flags = 0x7u, lodGroupId = 0xffffffffu;
};
/**
 * @brief Owning static-prefab GPU leases and immutable draw transforms; no ECS or Scene mutation.
 * @ownership Mesh/image factories must outlive this object and its explicit release; the factories own GPU allocations.
 * @lifetime Release before backend shutdown/device loss. Reimport builds a separate candidate; swap only after success.
 * @thread Creation, drawing, release and destruction are graphics-thread affine.
 * @reentrancy No scripts or caller callbacks and no locks; do not reenter from backend methods.
 */
class EVENGINE_API_WORLD EvpackStaticPrefab final {
public:
    /** @brief Evpack static prefab. */
    ~EvpackStaticPrefab();
    EvpackStaticPrefab(const EvpackStaticPrefab&)            = delete;
    EvpackStaticPrefab& operator=(const EvpackStaticPrefab&) = delete;
    /** @brief Validate a template and stage its mesh, material and image dependencies; failures release staged uploads.
     * @return Owning renderer candidate or structured error; existing renderers are untouched.
     * @param reader Borrowed only during load; returned data does not retain the reader or package.
     * @param meshes Borrowed backend factory that must outlive the returned object.
     * @param images Borrowed backend factory that must outlive the returned object.
     */
    [[nodiscard]] static Result<std::unique_ptr<EvpackStaticPrefab>> load(
        const asset::EvpackResourceReader& reader, graphics::IMeshResourceFactory& meshes,
        graphics::IImageResourceFactory& images, const AssetRef& asset, const asset::EvpackCapabilities& capabilities);
    /** @brief Number of enabled static renderer bindings prepared for drawing. */
    [[nodiscard]] std::size_t drawCount() const noexcept;
    /** @brief Copy prefab-local min XYZ/max XYZ enclosing enabled renderer geometry and hierarchy transforms.
     * @param rootPose Source-root convention, identical to the scene preparation convention.
     * @return Owning CPU-derived bounds, or NotFound when no enabled renderer exists.
     * @cost Constant-time copy; vertex scanning and transformed corner aggregation are amortized at load.
     * @thread Any thread after successful load; no callbacks. Bounds remain valid after GPU release.
     * @remarks Conservative transformed mesh AABB; excludes animated deformation and instance transforms.
     */
    [[nodiscard]] Result<std::array<float, 6>> localBounds(PrefabRootPose rootPose = PrefabRootPose::Authored) const;
    /** @brief Copy the admitted source asset identity; immutable and valid after GPU release.
     * @thread Any thread after successful load; caller keeps this object alive. No callbacks or IO.
     */
    [[nodiscard]] AssetRef sourceAsset() const;
    /** @brief Copy the admitted deterministic package build identity; constant-time, no IO or callbacks.
     * @thread Any thread after successful load; caller keeps this object alive, including after GPU release.
     */
    [[nodiscard]] PersistentId sourceBuildId() const noexcept;
    /** @brief Retain loaded resources for an admitted version without creating renderer/material inputs.
     * @param owner Owning live prefab reference, retained by a successful pin; factories outlive the returned token.
     * @return Owning pin, or released/null/overflow failure without mutation.
     * @cost Fixed allocation and counter increment per admitted version, after explicit expensive load.
     * @thread Graphics-owner affine, synchronous, non-reentrant, no scripts or locks.
     */
    [[nodiscard]] static Result<std::unique_ptr<StaticPrefabResourcePin>> pinResources(
        const std::shared_ptr<EvpackStaticPrefab>& owner);
    /** @brief Prepare complete scene-renderer materials and local transforms while pinning loaded prefab resources.
     * @param owner Owning prefab reference, borrowed during preparation and retained only by a successful lease.
     * @param graphics Borrowed graphics owner used for material-record cleanup; must outlive the returned lease.
     * @param rootPose Placement convention; replaces root translation/rotation only when explicitly selected.
     * @return Owning preparation lease or a structured error; no ECS/Scene mutation occurs.
     * @cost Linear in enabled renderer count, with material allocations amortized per prefab preparation.
     * @thread Graphics thread only; synchronous, non-reentrant, no scripts or locks.
     */
    [[nodiscard]] static Result<std::unique_ptr<PreparedStaticPrefabScene>> prepareScene(
        const std::shared_ptr<EvpackStaticPrefab>& owner, graphics::Graphics& graphics,
        PrefabRootPose rootPose = PrefabRootPose::Authored);
    /** @brief Register every opaque or masked prefab draw in GPU-driven tables transactionally.
     * @param graphics Borrowed backend corresponding to the factories used during load.
     * @return Owning part descriptors; material objects remain owned by this prefab until release.
     * @thread Graphics thread only; synchronous and non-reentrant.
     */
    [[nodiscard]] Result<std::vector<StaticPrefabGpuPart>> prepareGpuDriven(graphics::Graphics& graphics);
    /** @brief Draw in an already-open 3D pass using its camera/light state and a column-major instance transform.
     * @param graphics Borrowed backend corresponding to the factories used at load.
     * @param transform Finite column-major instance transform; borrowed only during this call.
     * @param cameraView Finite right-handed column-major view matrix, borrowed for per-prefab transparent sorting.
     * @return Success or invalid-transform/released-state error before issuing draws.
     * @remarks Draws opaque PBR first, then transparent submeshes back-to-front by node origin within this prefab;
     * caller orders separate prefab instances for transparency. Sets material state; caller restores any state needed
     * by subsequent custom draws. No shadow pass is submitted.
     */
    [[nodiscard]] Result<void> draw(graphics::Graphics& graphics, const std::array<float, 16>& transform,
                                    const std::array<float, 16>& cameraView) const;
    /**
     * @brief Submit enabled source shadow casters inside an active cascade shadow pass.
     * @param graphics Borrowed backend corresponding to the factories used at load.
     * @param transform Finite column-major prefab instance transform.
     * @param lightViewProjection Finite column-major cascade light view-projection matrix.
     * @return Success, or a structured released/transform error before issuing draws.
     * @thread Graphics thread only; synchronous and non-reentrant.
     */
    [[nodiscard]] Result<void> drawShadow(graphics::Graphics& graphics,
                                          const std::array<float, 16>& transform,
                                          const std::array<float, 16>& lightViewProjection) const;
    /** @brief Invalidate draw packets and release all leases; failed releases remain owned for retry.
     * @return Backend failure if any release fails. Destructor retries and reports remaining errors to stderr.
     */
    [[nodiscard]] Result<void> release();

private:
    friend class PreparedStaticPrefabScene;
    friend class StaticPrefabResourcePin;
    struct Impl;
    explicit EvpackStaticPrefab(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
