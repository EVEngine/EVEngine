#pragma once

#include "common/Result.h"
#include "graphics/Color.h"
#include "stylize/MeshVfxBatchSubmission.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshParticleEmitter.h"
#include "stylize/TrailEffect.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
class Mesh;
class Shader;
class Texture;
}

namespace eve::stylize {

/** @brief Immediate borrowed inputs for drawing an effect over an existing mesh. */
struct MeshEffectDrawSource {
    MeshEffectTargetHandle target;
    graphics::Mesh* mesh = nullptr;
    graphics::Texture* albedo = nullptr;
    glm::mat4 model{1.f};
    graphics::Color tint{1.f};
};

/** @brief Borrowed mesh/material plus owning transform state for one mesh particle draw. */
struct MeshParticleDrawSource {
    graphics::Mesh* mesh = nullptr;
    graphics::Texture* albedo = nullptr;
    MeshParticleInstance instance;
};

/** @brief Packed owning arrays accepted by Graphics dynamic mesh upload APIs. */
struct TrailUploadData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<std::uint32_t> indices;
};

/** @brief Observable result of one mesh-effect submission. */
enum class MeshEffectSubmitStatus { Drawn, SkippedInactive, SkippedEmpty };

/** @brief Concrete draw payload resolved for one batched stable instance ID. */
struct MeshVfxRendererCommand {
    enum class Kind : std::uint8_t { Overlay, Trail, Particle };

    std::uint64_t stableInstanceId = 0;
    Kind kind = Kind::Overlay;
    MeshEffectInstance* effect = nullptr;
    MeshEffectDrawSource overlay;
    TrailMeshSnapshot trail;
    glm::mat4 trailModel{1.f};
    graphics::Color trailTint{1.f};
    MeshParticleDrawSource particle;
};

/** @brief Complete planner and renderer inputs generated from one particle snapshot. */
struct MeshParticleRenderInputs {
    std::vector<MeshVfxLodCandidate> lodCandidates;
    std::vector<MeshVfxRenderItem> renderItems;
    std::vector<MeshVfxRendererCommand> commands;
};

/**
 * @brief Convert stable particle snapshots into the shared LOD/batch/render pipeline.
 * @param instances Immutable simulation snapshot.
 * @param effect Borrowed active style instance retained only in returned immediate commands.
 * @param mesh Borrowed backend mesh shared by every generated command.
 * @param albedo Optional borrowed albedo shared by every generated command.
 * @param key Shared immutable GPU state identity; meshId must identify mesh.
 * @param emitterStableId Stable namespace for particle IDs across emitters.
 * @param cameraPosition World-space camera position used for distance/depth ordering.
 * @param projectionScalePixels Approximate projected pixels for one world unit at distance one.
 * @param maximumDistance Visibility cutoff copied into generated candidates.
 * @param priority Budget priority copied into generated candidates.
 * @return Owning vectors with matching stable IDs and no retained snapshot references.
 */
[[nodiscard]] MeshParticleRenderInputs buildMeshParticleRenderInputs(
    std::span<const MeshParticleInstance> instances, MeshEffectInstance* effect,
    graphics::Mesh* mesh, graphics::Texture* albedo, const MeshVfxBatchKey& key,
    std::uint64_t emitterStableId, const glm::vec3& cameraPosition,
    float projectionScalePixels, float maximumDistance, std::int32_t priority = 0);

/**
 * @brief Validate and convert a ribbon snapshot into packed graphics arrays.
 * @param snapshot Owning CPU trail snapshot; it is not retained.
 * @return Packed owning arrays or a structured invalid-geometry failure.
 * @thread Thread-safe; performs CPU work only.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] eve::Result<TrailUploadData> prepareTrailUpload(const TrailMeshSnapshot& snapshot);

/**
 * @brief Render-thread adapter from stylize runtime state to Graphics mesh draws.
 *
 * Graphics owns every mesh and shader created by this adapter. The adapter
 * retains borrowed backend handles across frames, so its Graphics owner must
 * outlive it and the adapter must only be used with that same owner. Destruction
 * does not perform fallible GPU work; resources remain owned by Graphics and
 * are reclaimed at Graphics shutdown.
 */
class MeshEffectRenderer final : public IMeshVfxBatchSink {
public:
    /** @brief Bind this adapter to one Graphics owner for its entire lifetime. */
    explicit MeshEffectRenderer(graphics::Graphics& graphics) noexcept;

    MeshEffectRenderer(const MeshEffectRenderer&) = delete;
    MeshEffectRenderer& operator=(const MeshEffectRenderer&) = delete;

    /**
     * @brief Draw a second-pass effect over a resolved source mesh.
     * @param effect Active effect whose target must equal source.target.
     * @param source Immediate borrowed mesh/material/transform inputs.
     * @return Drawn, inactive, or a structured stale/invalid failure.
     * @thread Render-thread affine; requires an open 3D frame.
     */
    [[nodiscard]] eve::Result<MeshEffectSubmitStatus> submitOverlay(
        MeshEffectInstance& effect, const MeshEffectDrawSource& source);

    /**
     * @brief Upload and draw one dynamic ribbon using an active mesh effect.
     * @param effect Active mesh effect, normally using the slash style.
     * @param snapshot Owning CPU geometry snapshot; it is not retained.
     * @param model Model transform; use identity for world-space trail samples.
     * @param tint Per-draw color multiplied by the lifecycle envelope.
     * @return Drawn, empty/inactive, or a structured upload failure.
     * @thread Render-thread affine; requires an open 3D frame.
     */
    [[nodiscard]] eve::Result<MeshEffectSubmitStatus> submitTrail(
        MeshEffectInstance& effect, const TrailMeshSnapshot& snapshot,
        const glm::mat4& model = glm::mat4(1.f), const graphics::Color& tint = graphics::Color(1.f));

    /**
     * @brief Draw one simulated mesh particle through an active mesh-effect style.
     * @param effect Shared active effect style; no character target binding is required.
     * @param source Immediate particle transform and borrowed mesh/material handles.
     * @return Drawn, inactive, or a structured invalid-source failure.
     * @thread Render-thread affine; requires an open 3D frame.
     */
    [[nodiscard]] eve::Result<MeshEffectSubmitStatus> submitParticle(
        MeshEffectInstance& effect, const MeshParticleDrawSource& source);

    /**
     * @brief Resolve and submit a complete LOD-planned render queue.
     * @param queue Ordered batches produced for this frame.
     * @param commands Owning/immediate command payloads keyed by stableInstanceId.
     * @return Submission report or invalid/reentrant command failure.
     * @thread Render-thread affine; requires an open 3D frame.
     * @reentrancy Must not be called recursively on the same renderer.
     */
    [[nodiscard]] eve::Result<MeshVfxSubmissionReport> submitQueue(
        const MeshVfxRenderQueue& queue, std::span<const MeshVfxRendererCommand> commands);

    /** @brief Begin one GPU-state batch; intended for MeshVfxBatchExecutor. */
    MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey& key) override;

    /** @brief Submit one resolved draw in the active batch. */
    MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw& draw) override;

    /** @brief End the active GPU-state batch. */
    MeshVfxSubmitStatus endBatch() override;

    /** @brief Return the backend-owned dynamic ribbon mesh, or nullptr before first upload. */
    [[nodiscard]] graphics::Mesh* trailMesh() const noexcept { return trailMesh_; }

private:
    graphics::Shader* shaderFor(MeshEffectInstance& effect);

    graphics::Graphics& graphics_;
    graphics::Mesh* trailMesh_ = nullptr;
    std::unordered_map<std::string, graphics::Shader*> shaders_;
    std::unordered_map<std::uint64_t, const MeshVfxRendererCommand*> activeCommands_;
    bool batchActive_ = false;
};

}  // namespace eve::stylize
