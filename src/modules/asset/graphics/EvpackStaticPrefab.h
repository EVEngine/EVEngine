#pragma once

#include <array>
#include <memory>
#include "common/ResourceRef.h"

namespace eve::asset {
class EvpackResourceReader;
struct EvpackCapabilities;
}  // namespace eve::asset
namespace eve::graphics {
class Graphics;
class IMeshResourceFactory;
class IImageResourceFactory;
}  // namespace eve::graphics

namespace eve::asset_graphics {
/**
 * @brief Owning static-prefab GPU leases and immutable draw transforms; no ECS or Scene mutation.
 * @ownership Mesh/image factories must outlive this object and its explicit release; the factories own GPU allocations.
 * @lifetime Release before backend shutdown/device loss. Reimport builds a separate candidate; swap only after success.
 * @thread Creation, drawing, release and destruction are graphics-thread affine.
 * @reentrancy No scripts or caller callbacks and no locks; do not reenter from backend methods.
 */
class EvpackStaticPrefab final {
public:
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
    /** @brief Invalidate draw packets and release all leases; failed releases remain owned for retry.
     * @return Backend failure if any release fails. Destructor retries and reports remaining errors to stderr.
     */
    [[nodiscard]] Result<void> release();

private:
    struct Impl;
    explicit EvpackStaticPrefab(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
