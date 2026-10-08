#pragma once
#include <cstdint>
#include <memory>
#include "common/Result.h"

namespace eve {
class AssetRef;
}
namespace eve::asset {
struct CanonicalMeshData;
struct CanonicalMeshLimits;
struct EvpackCapabilities;
class EvpackResourceReader;
}  // namespace eve::asset
namespace eve::graphics {
class Graphics;
class Mesh;
class VegetationField;
struct VegetationMotion;
struct VegetationGeometry;
}  // namespace eve::graphics
namespace eve::asset_graphics {
/** @brief Owning decoded TVE rest geometry, independent of archive and GPU resource lifetimes.
 * Immutable after construction. CPU evaluation permits concurrent readers; GPU calls
 * require the graphics owner thread. No callbacks or external pointers are retained.
 */
class EVENGINE_API_WORLD VegetationAsset {
public:
    ~VegetationAsset();
    VegetationAsset(const VegetationAsset&)            = delete;
    VegetationAsset& operator=(const VegetationAsset&) = delete;
    /** @brief Admit explicit TVE-packed Unity canonical streams as native rest geometry.
     * @return Owning asset or checked missing-channel/validation failure, without partial publication.
     */
    [[nodiscard]] static Result<std::unique_ptr<VegetationAsset>> fromCanonical(const asset::CanonicalMeshData& mesh);
    /** @brief Read one canonical runtime mesh and decode its TVE authoring streams.
     * Reader, identity, capabilities and limits are borrowed only for this synchronous call.
     * @return Owning asset with no retained archive references or GPU allocations.
     */
    [[nodiscard]] static Result<std::unique_ptr<VegetationAsset>> load(const asset::EvpackResourceReader& reader,
                                                                       const AssetRef&                    ref,
                                                                       const asset::EvpackCapabilities&   capabilities,
                                                                       const asset::CanonicalMeshLimits&  limits);
    /** @brief Return the immutable rest vertex count. */
    std::uint32_t vertexCount() const noexcept;
    /** @brief Evaluate owning world-space geometry with explicit field/time/transform inputs.
     * Worker-safe with immutable inputs; caller inputs are immediate borrows.
     */
    [[nodiscard]] Result<graphics::VegetationGeometry> evaluate(const graphics::VegetationField&  field,
                                                                const graphics::VegetationMotion& motion) const;
    /** @brief Create a backend-owned animated mesh; caller must release it through graphics.
     * Graphics-thread only. Returned borrow lasts until release, device loss or graphics shutdown.
     * @return Complete mesh or checked failure. No mesh is retained by this asset.
     */
    [[nodiscard]] Result<graphics::Mesh*> createMesh(graphics::Graphics&               graphics,
                                                     const graphics::VegetationField&  field,
                                                     const graphics::VegetationMotion& motion) const;
    /** @brief Create an object-space rest mesh with owning GPU-deformation factors.
     * Graphics-thread only. The returned backend mesh contains immutable rest
     * positions plus shading and nine-float TVE deformation streams. Caller
     * must release it through graphics. No field, clock, or transform is read.
     * @return Complete mesh or checked upload/attachment failure with rollback.
     */
    [[nodiscard]] Result<graphics::Mesh*> createGpuFieldMesh(graphics::Graphics& graphics) const;
    /** @brief Update an existing mesh from immutable rest data, without accumulating deformation.
     * Graphics-thread only. Mesh and graphics are borrowed for the call, never retained.
     * Draw the resulting world-space mesh with an identity model transform.
     * @return Checked validation/backend failure or complete updated geometry and bounds.
     */
    [[nodiscard]] Result<void> updateMesh(graphics::Graphics& graphics, graphics::Mesh& mesh,
                                          const graphics::VegetationField&  field,
                                          const graphics::VegetationMotion& motion) const;

private:
    struct Impl;
    explicit VegetationAsset(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
