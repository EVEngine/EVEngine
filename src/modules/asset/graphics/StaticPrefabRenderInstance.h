#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::graphics {
class Renderable3D;
}
namespace eve::asset_graphics {
class PreparedStaticPrefabScene;
/** @brief Own renderer entities and their prepared prefab resources, without owning a Scene node.
 * @ownership The ECS table owns entity storage; this adapter owns entity lifecycles and the preparation lease.
 * @lifetime The ECS table, Graphics and resource factories must outlive this object. Unlink Scene links before release.
 * @thread Graphics/ECS-owner thread only, outside view iteration for creation; no scripts, callbacks or locks.
 * @remarks Runtime handles detect external entity deletion/reuse. Reimport creates a separate candidate before swap.
 */
class EVENGINE_API_WORLD StaticPrefabRenderInstance final {
public:
    /** @brief Destroy surviving renderer entities before releasing their material/mesh preparation lease. */
    ~StaticPrefabRenderInstance();
    StaticPrefabRenderInstance(const StaticPrefabRenderInstance&)            = delete;
    StaticPrefabRenderInstance& operator=(const StaticPrefabRenderInstance&) = delete;
    /** @brief Consume preparation and publish all renderer parts after validating composed transforms.
     * @param preparation Owning preparation consumed on success or failure; existing instances are untouched.
     * @param world Finite column-major affine instance matrix, composed with each complete prefab-local matrix.
     * @return Owning renderer instance or structured validation/allocation failure with candidate entities removed.
     * @cost Linear in renderer count, allocating ECS entities once per instance after explicit prefab preparation.
     * @remarks Rejects deferred ECS creation. No Scene mutation or automatic asset lookup occurs.
     */
    [[nodiscard]] static Result<std::unique_ptr<StaticPrefabRenderInstance>> create(
        std::unique_ptr<PreparedStaticPrefabScene> preparation, const std::array<float, 16>& world);
    /** @brief Update all composed renderer matrices after validating every matrix and live entity handle.
     * @return Success, invalid matrix, released state or StaleHandle; failures leave surviving renderer poses
     * unchanged.
     * @cost Linear in prepared renderer count; no allocations, asset resolution or GPU upload.
     */
    [[nodiscard]] Result<void> setWorldTransform(const std::array<float, 16>& world);
    /** @brief Return the number of owned renderer handles; externally deleted entries remain counted until release. */
    [[nodiscard]] std::size_t rendererCount() const noexcept;
    /** @brief Resolve a renderer for synchronous Scene linking or diagnostics.
     * @return Borrowed renderer or NotFound/StaleHandle; never transfers entity ownership.
     * @ownership ECS owns storage. @lifetime Valid until entity deletion, table mutation or instance release.
     * @remarks Do not retain the pointer across frames; Scene links must be removed before adapter release.
     */
    [[nodiscard]] Result<graphics::Renderable3D*> renderer(std::size_t index) const;
    /** @brief Destroy surviving entities, then release preparation; cleanup failure retains the lease for retry.
     * @return Success or backend cleanup error. Repeated successful release is harmless.
     */
    [[nodiscard]] Result<void> release();

private:
    struct Impl;
    explicit StaticPrefabRenderInstance(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
