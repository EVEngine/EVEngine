#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::physics {

class GeometryCollectionInstance;
class CollapseAnimation;
class CollapsePlayback;
struct FractureGeometry;
struct BonePresentation;

/**
 * @brief Presentation bridge for a borrowed geometry-collection instance.
 *
 * Fractured collections use prepared exterior/cut-face mesh batches. Legacy box
 * collections keep per-bone proxies and revision-based sleeping batches.
 * Animation submission copies poses into renderer-owned scratch before the
 * engine forward-pass callback; graphics must outlive the renderer.
 * Current submitted meshes also fill the same-frame GBuffer for fog occlusion.
 * One process-lifetime dispatcher holds weak registrations; renderer teardown
 * expires its registration before releasing meshes. Render-thread only, no reentrant teardown.
 *
 * @ownership Never owns Instance, Graphics, or Body3D. Mesh caches are produced
 * through Graphics and not retained as exclusive ownership.
 * @lifetime Borrows Instance and Graphics only for each call; do not call draw
 * after Instance or its World3D is destroyed.
 */
class EVENGINE_API_DOMAINS GeometryCollectionRenderer final {
public:
    /**
     * @brief Construct a renderer that borrows an instance.
     * @param instance Borrowed instance, or null to disable drawing.
     * @ownership Does not take ownership of instance.
     * @lifetime instance must outlive draws, or be cleared via setInstance(null).
     */
    explicit GeometryCollectionRenderer(GeometryCollectionInstance* instance) noexcept;

    GeometryCollectionRenderer(const GeometryCollectionRenderer&)            = delete;
    GeometryCollectionRenderer& operator=(const GeometryCollectionRenderer&) = delete;
    /** @brief Geometry collection renderer. */
    ~GeometryCollectionRenderer();

    /**
     * @brief Replace the borrowed instance; null disables drawing.
     * @ownership Does not take ownership of instance.
     * @lifetime instance must outlive subsequent draws, or be cleared again.
     */
    void setInstance(GeometryCollectionInstance* instance);
    /**
     * @brief Return the currently borrowed instance.
     * @ownership Borrowed; caller must not delete through this pointer.
     * @lifetime Valid until setInstance or renderer destruction.
     */
    [[nodiscard]] GeometryCollectionInstance* getInstance() const noexcept { return instance_; }

    /** @brief Exterior-face RGBA; also used for Attached legacy box bones. */
    void setExteriorColor(float r, float g, float b, float a);
    /** @brief Cut-face RGBA; also used for Detached legacy box bones. */
    void setInteriorColor(float r, float g, float b, float a);
    /** @brief Set exterior RGBA for one authored structural group (clusterId).
     * @param groupId Author-defined group in [0,4095].
     * @param r Red linear color in [0,1].
     * @param g Green linear color in [0,1].
     * @param b Blue linear color in [0,1].
     * @param a Alpha in [0,1].
     * @return InvalidArgument leaves palette and prepared batches unchanged.
     * @ownership Renderer owns color values; no borrowed resources.
     * @thread Render thread, synchronous and reentrant-free; no callbacks.
     * @cost Constant-size edit per matching material batch; no mesh rebuild/upload.
     */
    [[nodiscard]] eve::Result<void> setGroupExteriorColor(int groupId, float r, float g, float b, float a);
    /** @brief Sleeping-batch RGBA. */
    void setSleepColor(float r, float g, float b, float a);

    /**
     * @brief Synchronize caches and draw all bones into the current 3D frame.
     * @param graphics Borrowed Graphics used for mesh allocation and drawMesh.
     * @return Success, or StaleHandle/InvalidArgument when the instance/world is gone.
     * @ownership Does not take ownership of graphics.
     * @lifetime graphics must remain live for the duration of this call.
     */
    [[nodiscard("check geometry-collection draw")]] eve::Result<void> draw(graphics::Graphics* graphics);

    /** @brief Number of bones drawn individually on the last successful draw. */
    [[nodiscard]] int lastActiveDrawCount() const noexcept { return lastActiveDrawCount_; }
    /** @brief Number of bones included in the sleeping batch on the last draw. */
    [[nodiscard]] int lastSleepBatchCount() const noexcept { return lastSleepBatchCount_; }

    /** @brief Draw a baked animation without a live physics instance.
     * @param animation Borrowed only for this call; caller owns the asset.
     * @param graphics Borrowed graphics, must outlive renderer mesh caches.
     * @param seconds Playback time; endpoints clamp, caller controls speed/loop/seek.
     * @param offsetX Instance translation in metres.
     * @param offsetY Instance translation in metres.
     * @param offsetZ Instance translation in metres.
     * @return Checked draw status; single render thread, no callbacks.
     */
    [[nodiscard]] eve::Result<void> drawAnimation(const CollapseAnimation& animation, graphics::Graphics* graphics,
                                                  float seconds, float offsetX, float offsetY, float offsetZ);

    /** @brief Upload embedded meshes once before baked playback.
     * @param animation Borrowed during call; immutable geometry ownership is shared by caches.
     * @param graphics Borrowed graphics owner, must outlive renderer.
     * @return Failure releases candidate meshes and retains the previous prepared asset.
     * @cost GPU allocation proportional to triangles; prepare once per renderer/asset, never per frame.
     * @thread Render thread; no callbacks or locks.
     */
    [[nodiscard]] eve::Result<void> prepareAnimation(const CollapseAnimation& animation, graphics::Graphics* graphics);
    /** @brief Upload the bound live instance geometry before drawing it.
     * @cost Same offline GPU allocation as prepareAnimation, amortized over live draws.
     * @return Checked status; same ownership/thread contract as prepareAnimation.
     */
    [[nodiscard]] eve::Result<void> prepareInstance(graphics::Graphics* graphics);

    /** @brief Queue one sampled mesh animation for the engine's next main forward and CSM shadow passes.
     * @return Checked status; call after prepareAnimation and before gfx.render3D.
     * @ownership Samples are copied immediately; no clip pointer is retained across frames.
     * @thread Render thread; renderer owns forward/shadow contributor tokens and unregisters both before destruction.
     * @cost Linear pose sampling and vertex upload once per frame; all passes reuse prepared GPU resources.
     */
    [[nodiscard]] eve::Result<void> submitAnimation(const CollapseAnimation& animation, float seconds, float offsetX,
                                                    float offsetY, float offsetZ);

    /** @brief Prepare meshes for a frozen player without retaining its clock.
     * @ownership Borrows player during the call; shares its immutable geometry. Graphics outlives renderer.
     * @thread Render thread only, no callbacks.
     * @cost GPU allocation proportional to triangles; prepare once per renderer and asset.
     * @return Same checked preparation status as prepareAnimation.
     */
    [[nodiscard]] eve::Result<void> preparePlayback(const CollapsePlayback& player, graphics::Graphics* graphics);
    /** @brief Submit this player's placed poses to forward, shadow and depth passes.
     * @ownership Copies samples during call; no player reference survives the call.
     * @thread Render thread only, no callbacks. Failed submission disables the prior submission.
     * @cost Linear sampling and vertex upload per frame; prepared resources are reused across passes.
     * @return Checked status; requires preparePlayback and embedded mesh geometry.
     */
    [[nodiscard]] eve::Result<void> submitPlayback(const CollapsePlayback& player);

private:
    [[nodiscard]] eve::Result<void> beginAnimationSubmission(const CollapseAnimation& animation);
    [[nodiscard]] eve::Result<void> finishAnimationSubmission();
    struct DepthRegistration;
    std::shared_ptr<DepthRegistration> depthRegistration_;
    [[nodiscard]] eve::Result<void>    registerDepthContribution();
    std::uint64_t                      forwardToken_ = 0;
    std::uint64_t                      shadowToken_  = 0;
    bool                               submitted_    = false;
    [[nodiscard]] eve::Result<void>    prepareGeometry(std::shared_ptr<const FractureGeometry> geometry,
                                                       graphics::Graphics* graphics, const std::vector<int>& groups);
    [[nodiscard]] eve::Result<void>    updateGeometry(graphics::Graphics& graphics);
    [[nodiscard]] eve::Result<void>    drawGeometry(graphics::Graphics& graphics);
    struct FragmentBatch {
        graphics::Mesh*            mesh     = nullptr;
        int                        groupId  = 0;
        bool                       interior = false;
        std::vector<float>         restPositions, restNormals, positions, normals, uv;
        std::vector<std::uint32_t> indices, bones;
    };
    void                                    releaseFragmentMeshes(graphics::Graphics* graphics);
    std::shared_ptr<const FractureGeometry> preparedGeometry_;
    std::vector<FragmentBatch>              fragmentBatches_;
    std::map<int, std::array<float, 4>>     groupColors_;
    std::vector<BonePresentation>           poseScratch_;
    std::vector<std::array<float, 16>>      boneMatrices_;
    void ensureUnitBox(graphics::Graphics& graphics);
    void releaseOwnedMeshes(graphics::Graphics* graphics);
    void invalidateSleepBatch(graphics::Graphics* graphics = nullptr);
    [[nodiscard]] eve::Result<void> rebuildSleepBatch(graphics::Graphics& graphics);

    GeometryCollectionInstance* instance_ = nullptr;
    /** @brief Borrowed Graphics that owns cached meshes; used only for releaseMesh. */
    graphics::Graphics* meshOwner_ = nullptr;
    graphics::Mesh* unitBox_ = nullptr;
    graphics::Mesh* sleepBatch_ = nullptr;
    std::uint64_t sleepBatchRevision_ = 0;
    int sleepBatchBoneCount_ = 0;
    int lastActiveDrawCount_ = 0;
    int lastSleepBatchCount_ = 0;
    float exteriorR_ = 0.62f, exteriorG_ = 0.58f, exteriorB_ = 0.52f, exteriorA_ = 1.f;
    float interiorR_ = 0.78f, interiorG_ = 0.42f, interiorB_ = 0.28f, interiorA_ = 1.f;
    float sleepR_ = 0.45f, sleepG_ = 0.45f, sleepB_ = 0.48f, sleepA_ = 1.f;
};

}  // namespace eve::physics
