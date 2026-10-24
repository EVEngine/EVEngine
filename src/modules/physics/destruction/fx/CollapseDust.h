#pragma once
#include <array>
#include <future>
#include <glm/mat4x4.hpp>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::fluids {
class VolumeFluid;
}  // namespace eve::fluids
namespace eve::graphics {
class Camera3D;
class AtmosphereVolume;
class Graphics;
class Texture;
class Volumetric;
}  // namespace eve::graphics
namespace eve::physics {
class CollapseAnimation;
/** @brief Owning camera snapshot; CPU workers never access an ECS camera. */
struct DustFogView {
    glm::mat4 view{1.f}, inverseViewProjection{1.f};
    float     fov = 39.f, nearDistance = 0.2f, farDistance = 65.f;
};
/** @brief Owning worker output; no GPU or source pointers. Graphics borrows pixels only for upload. */
struct DustFogFrame {
    std::vector<unsigned char> atlas, depth;
    int                        atlasWidth = 0, atlasHeight = 0, depthWidth = 0, depthHeight = 0;
    DustFogView                view;
    float                      seconds = 0.f;
};
/** @brief Explicit worker admission state. */
enum class DustFogRequest { Started, Busy, Unchanged };
/** @brief Whether a matching-view fog image is available for compositing. */
enum class DustFogDisplay { Pending, Ready };
/** @brief Owning gas presentation cache, independent of the source solver.
 * @ownership Owns frames and depth scratch; borrows arguments synchronously only.
 * @thread Single caller, no callbacks or concurrent mutation.
 * @details EVEDUST1 version 1, little endian float32; unknown versions/fields and trailing bytes rejected.
 * Load validates into a candidate before replacing this clip. Playback restores a dedicated render-only
 * fluid, never a gameplay solver. Fixed-step fluid bake is repeatable on the same build; saved playback
 * has no simulation or RNG. Particle births/deaths use recorded frame boundaries.
 */
class EVENGINE_API_DOMAINS CollapseDust {
public:
    /** @brief Construct an empty owning cache; no worker starts until requested. */
    CollapseDust();
    /** @brief Join and check an unpublished CPU job before releasing owned data. */
    ~CollapseDust();
    CollapseDust(const CollapseDust&)            = delete;
    CollapseDust& operator=(const CollapseDust&) = delete;
    CollapseDust(CollapseDust&&)                 = delete;
    CollapseDust& operator=(CollapseDust&&)      = delete;
    /** @brief Capture gas particles, fading their final two seconds.
     * @cost Linear in live particles, offline only. @return Checked status; failure leaves frames unchanged.
     */
    [[nodiscard]] Result<void> capture(const fluids::VolumeFluid& source, float seconds);
    /** @brief Restore nearest preceding frame into a dedicated presentation solver.
     * @cost Linear in particles, at the baked display rate; no solver step. @return Transactional restore status.
     */
    [[nodiscard]] Result<void> present(fluids::VolumeFluid& target, float seconds) const;
    /** @brief Save a new asset, refusing overwrite; incomplete writes are removed.
     * @cost Linear in all samples, once after bake. @return Checked IO status.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Validate and load an owning clip; failure preserves this clip.
     * @cost Linear in file size, once at startup. @return Checked IO/schema status.
     */
    [[nodiscard]] Result<void> load(std::string_view path);
    /** @brief Recorded frame count. */
    [[nodiscard]] int frameCount() const { return static_cast<int>(frames_.size()); }
    /** @brief Allocate bounded CPU scene-depth scratch before rendering.
     * @cost Linear in output pixels, once per resolution. @return Checked dimensions status.
     */
    [[nodiscard]] Result<void> prepareDepth(int width, int height);
    /** @brief Inject saved gas into the existing local-volume fog grid, without stepping a solver.
     * @param inverseViewProjection Borrowed inverse camera matrix matching the grid.
     * @param seconds Recorded playback time; finite, clamped to clip endpoints.
     * @param density Nonnegative extinction multiplier, at most 10.
     * @param radius Local fog support radius in metres, in [0.1,5].
     * @cost Bounded projected particle footprints, once per displayed gas frame; grid must be prepared by caller.
     * @return Checked dimensions/parameters status. No references survive this call.
     */
    [[nodiscard]] Result<void> injectFog(graphics::AtmosphereVolume& grid, const glm::mat4& inverseViewProjection,
                                         float seconds, float density, float radius) const;
    /** @brief Upload exact animated-fragment depth for the existing fog compositor.
     * @cost Linear in triangles and covered pixels; prepareDepth first, reuse while time/camera unchanged.
     * @return Checked raster/upload status. Graphics owns the caller-supplied texture; borrowed only here.
     */
    [[nodiscard]] Result<void> updateDepthTexture(graphics::Graphics& graphics, graphics::Texture& texture,
                                                  const CollapseAnimation& clip, float seconds,
                                                  graphics::Camera3D& camera);

    /** @brief Prepare independent clip ownership and bounded worker dimensions.
     * @cost Copies all rigid samples once; call outside playback. @return Checked preparation status.
     * @ownership Owns a clip copy; source may be destroyed. An outstanding job must be collected first.
     */
    [[nodiscard]] Result<void> prepareFog(const CollapseAnimation& clip, int depthWidth, int depthHeight, int gridWidth,
                                          int gridHeight, int gridDepth);
    /** @brief Submit at most one CPU-only worker; busy requests do not queue more work.
     * @cost Copies one gas frame and starts bounded background reconstruction; no GPU calls or waits.
     * @return Started, Busy or Unchanged; checked invalid input never submits work.
     * @thread Main caller only; worker captures owning values and invokes no callbacks.
     */
    [[nodiscard]] Result<DustFogRequest> requestFog(float seconds, const DustFogView& view, float density,
                                                    float radius);
    /** @brief Nonblocking collection; an empty optional means the sole worker is still running.
     * @return Owning pixels, pending, or checked worker failure; no partial GPU publication.
     */
    [[nodiscard]] Result<std::optional<DustFogFrame>> takeFogFrame();
    /** @brief Poll/upload completed pixels and request the latest time without blocking rendering.
     * @cost Bounded texture uploads only on completed work; CPU reconstruction is on the owned worker.
     * @return Ready only when uploaded fog matches this camera. Borrowed GPU arguments are never retained.
     * @lifetime Destruction joins the one owned CPU job, which retains no engine/service pointers.
     */
    [[nodiscard]] Result<DustFogDisplay> updateFog(graphics::Volumetric& fog, graphics::Graphics& graphics,
                                                   float seconds, graphics::Camera3D& camera, float density,
                                                   float radius);

private:
    [[nodiscard]] Result<void> rasterizeDepth(const CollapseAnimation& clip, float seconds, graphics::Camera3D& camera);
    [[nodiscard]] Result<void> rasterizeDepthView(const CollapseAnimation& clip, float seconds,
                                                  const DustFogView& view);
    struct Frame {
        float                             seconds;
        float                             spacing;
        std::vector<std::array<float, 7>> particles;
    };
    std::vector<Frame>                       frames_;
    std::vector<float>                       depth_;
    std::vector<unsigned char>               depthPixels_;
    int                                      width_ = 0, height_ = 0;
    std::shared_ptr<const CollapseAnimation> fogClip_;
    int                                      gridWidth_ = 0, gridHeight_ = 0, gridDepth_ = 0;
    float       requestedSeconds_ = -1.f, requestedDensity_ = -1.f, requestedRadius_ = -1.f;
    DustFogView requestedView_, presentedView_;
    bool        presented_ = false;
    // Declared last so the sole future joins before other member destruction.
    std::future<Result<DustFogFrame>> future_;
};
}  // namespace eve::physics
