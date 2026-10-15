#pragma once

#include <memory>
#include "common/Export.h"
#include "fluids/OceanSpray.h"

namespace eve::graphics {
class Graphics;
}
namespace eve::fluids {
/** @brief World camera basis and render origin; all positions use metres and Y-up. */
struct OceanSprayView {
    glm::dvec3 eye{0.0}, forward{0.0, 0.0, -1.0}, up{0.0, 1.0, 0.0}, origin{0.0};
};
/** @brief Owning batch; attributes store (coverage,0,0) in the mesh normal attribute. */
struct OceanSprayMeshFrame {
    std::vector<float>         positions, attributes, uv;
    std::vector<std::uint32_t> indices;
    double                     time = 0.0;
};
/** @brief Cheap borrowed projection; source must remain alive and immutable through build(). */
class OceanSprayMeshBuilder {
public:
    /** @brief Borrow source for synchronous build only; never retain across move/destruction. */
    explicit OceanSprayMeshBuilder(const OceanSprayFrame& source) : source_(source) {}
    OceanSprayView view;
    /** @brief Sort back-to-front and build camera-facing quads in a floating render origin.
     * @return Owning geometry or InvalidArgument; no callbacks, thread-safe const input.
     * @cost O(particles log particles) sorting and linear geometry; once per changed view/frame.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSprayMeshFrame> build() const;

private:
    const OceanSprayFrame& source_;
};
class OceanSprayRendererBuilder;
/** @brief Render-thread-only spray projection; never advances or owns simulation authority.
 * Graphics resources are provider-owned and guarded by its weak lifetime. Either destruction
 * order is safe. No callbacks; input geometry is borrowed only during publish.
 */
class OceanSprayRenderer {
public:
    EVENGINE_API_DOMAINS ~OceanSprayRenderer();
    OceanSprayRenderer(const OceanSprayRenderer&)            = delete;
    OceanSprayRenderer& operator=(const OceanSprayRenderer&) = delete;
    /** @brief Upload validated geometry to the inactive mesh, then publish it atomically.
     * @return Success or validation/provider/upload diagnostic; failure preserves the displayed batch.
     * @cost Linear vertex upload and backend synchronization; once per prepared render frame.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> publish(const OceanSprayMeshFrame& frame);
    /** @brief Draw one transparent, depth-tested batch after opaque water and hull geometry.
     * @return Success, Conflict if unpublished, or StaleHandle after provider retirement.
     * Camera must use the same render origin as the published frame. No depth writes.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> draw();

private:
    friend class OceanSprayRendererBuilder;
    OceanSprayRenderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
/** @brief Cheap borrowed graphics preparation; provider must survive build and later rendering. */
class OceanSprayRendererBuilder {
public:
    /** @brief Borrow graphics through build; created renderer observes its retirement thereafter. */
    explicit OceanSprayRendererBuilder(graphics::Graphics& graphics) : graphics_(graphics) {}
    /** @brief Create shaders and double-buffered meshes; retain no builder reference.
     * @cost Pipeline creation and small initial GPU allocations; amortize over renderer lifetime.
     * @return Owning renderer or explicit unsupported/provider diagnostic. Render-thread only.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<std::unique_ptr<OceanSprayRenderer>> build() const;

private:
    graphics::Graphics& graphics_;
};
}  // namespace eve::fluids
