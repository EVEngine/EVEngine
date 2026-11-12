#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "common/Export.h"
#include "physics/field/GravityField.h"

namespace eve::gpgpu {
class Gpgpu;
}
namespace eve::graphics {
class Graphics;
namespace multiscale {
class SphereBatch;
}
}  // namespace eve::graphics
namespace eve::physics::field::gpu {

/** @brief One finite binary32 point source in a shared view-relative coordinate frame. */
struct GpuSource {
    std::array<float, 3> position{};
    float                mass = 1;
};

/** @brief One independently integrated target; source and target identities are distinct. */
struct GpuMotion {
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
};

/** @brief Explicit accuracy and resource bounds for the eight-cluster GPU backend. */
struct GpuFieldPolicy {
    float       absoluteAccelerationError = 1e-3f;
    float       clusterRadius             = .04f;
    float       maxSourceDisplacement     = .001f;
    std::size_t maxSources                = 1000000;
    std::size_t maxTargets                = 1000000;
};

/** @brief Explicit owning readback of the published GPU state. */
struct GpuFieldSnapshot {
    FieldStamp             stamp;
    std::vector<GpuSource> sources;
    std::vector<GpuMotion> targets;
};

/** @brief Explicit binary32 camera and appearance for GPU projection of view-relative targets. */
struct GpuSphereProjection {
    std::array<float, 3> camera{};
    std::array<float, 3> right{1, 0, 0};
    std::array<float, 3> up{0, 1, 0};
    std::array<float, 3> forward{0, 0, 1};
    float                tangentX      = 1;
    float                tangentY      = 1;
    float                radius        = .05f;
    float                depthExponent = 904;
    std::array<float, 3> color{1, 1, 1};
};

/** @brief Per-target sphere appearance stored on the GPU between projection frames. */
struct GpuSphereAppearance {
    float                radius        = .05f;
    float                depthExponent = 904;
    std::array<float, 3> color{1, 1, 1};
};

/** @brief One target-indexed appearance replacement in a sorted edit batch. */
struct GpuSphereAppearanceEdit {
    std::uint32_t       targetIndex = 0;
    GpuSphereAppearance appearance;
};

class GpuGravitySession;

/**
 * @brief Single-use prepared GPU state publication right.
 * @ownership Owns no GPU buffer; dropping it cancels publication of its session's candidate.
 * @thread Graphics/GPGPU owner thread only; no callbacks or reentrancy.
 */
class EVENGINE_API_DOMAINS PreparedGpuGravityStep {
public:
    /** @brief Cancel this candidate if still current. */
    ~PreparedGpuGravityStep();
    PreparedGpuGravityStep(PreparedGpuGravityStep&&) noexcept;
    PreparedGpuGravityStep& operator=(PreparedGpuGravityStep&&) noexcept;
    PreparedGpuGravityStep(const PreparedGpuGravityStep&)            = delete;
    PreparedGpuGravityStep& operator=(const PreparedGpuGravityStep&) = delete;

private:
    friend class GpuGravitySession;
    struct Impl;
    explicit PreparedGpuGravityStep(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Optional GPU-resident two-stage field and target integrator for admitted compact clusters.
 * @ownership Sole owner of source/target device buffers and candidate publication. It never registers
 * with the existing physics loop or advances a rigid body.
 * @thread Graphics/GPGPU owner thread only, serialized and non-reentrant; no callbacks.
 * @lifetime Destroy before the Graphics provider. Both providers must remain alive throughout every call.
 * @remarks Fixed eight sign-of-coordinate clusters, Newtonian G=1, binary32 and semi-implicit Euler.
 * No distance cutoff. Inputs outside the radius, precision, error or capacity contract reject explicitly;
 * non-finite integrated results reject the candidate before publication.
 * The analytic force bound excludes binary32 roundoff. Cross-backend parity is tolerance-based.
 */
class EVENGINE_API_DOMAINS GpuGravitySession {
public:
    /** @brief Release all owned GPU resources; requires the Graphics provider to remain alive. */
    ~GpuGravitySession();
    GpuGravitySession(const GpuGravitySession&)            = delete;
    GpuGravitySession& operator=(const GpuGravitySession&) = delete;

    /**
     * @brief Validate independent source/target arrays, allocate GPU buffers and upload initial state.
     * @param compute Borrowed provider used synchronously for resource creation; must outlive the session.
     * @param graphics Borrowed Graphics provider whose lifetime is checked before each operation.
     * @param sources Borrowed finite positive masses; copied to device before return.
     * @param targets Borrowed positions and velocities; copied to device before return.
     * @param stamp Tick/revision identity of the initial state.
     * @param fixedStep Positive fixed simulation duration.
     * @param policy Explicit radius, approximation and row budgets.
     * @return Owning session or structured input/backend/budget failure.
     * @cost O(Ns+Nt) validation and upload, O(Ns+Nt) device storage plus bounded summaries;
     * pipeline creation is amortized over subsequent steps.
     */
    [[nodiscard]] static Result<std::unique_ptr<GpuGravitySession>> create(
        gpgpu::Gpgpu& compute, graphics::Graphics& graphics, std::span<const GpuSource> sources,
        std::span<const GpuMotion> targets, FieldStamp stamp, Duration fixedStep, GpuFieldPolicy policy);

    /**
     * @brief Build and evaluate one complete candidate on the GPU, leaving published buffers unchanged.
     * @param step Exactly the next tick and configured fixed duration.
     * @param sourceShift Bounded x displacement; adjacent source identities move in opposite directions.
     * @return Single-use publication token or structured rejection; failure consumes no simulation tick.
     * @cost O(Ns+Nt) GPU work for fixed eight clusters, one queue submission and one admission-word readback;
     * source/target data remain device resident. Worst-case wall time depends on queue and driver.
     */
    [[nodiscard]] Result<PreparedGpuGravityStep> prepareStep(SimulationStep step, float sourceShift);

    /**
     * @brief Atomically promote an admitted candidate after owner and stamp checks.
     * @return Success or stale/foreign candidate failure without changing published buffers.
     * @cost O(1) host buffer index swap; no GPU submission or per-row copy.
     */
    [[nodiscard]] Result<void> commit(PreparedGpuGravityStep step);

    /**
     * @brief Project the published GPU targets into a preallocated multiscale sphere batch.
     * @param batch Borrowed prepared view with capacity for every target; not retained.
     * @param projection Borrowed finite view-relative camera, orthonormal basis and uniform appearance.
     * @return Success after GPU packet admission and same-device image copy, or structured failure.
     * A failed projection does not change the batch draw count or published simulation state.
     * @ownership The session owns packet storage; the batch owns its image. Neither retains the other.
     * @thread Graphics/GPGPU owner thread only, outside a render pass, non-reentrant; no callbacks.
     * @cost O(Nt) GPU projection, one admission-word readback and O(batch capacity) device-local
     * image copy plus synchronization. Both prepared owners amortize allocation and pipelines.
     * @remarks Targets use one finite view-relative binary32 frame; this is not a global multiscale
     * camera conversion. Near-plane uncertainty rejects the complete view.
     */
    [[nodiscard]] Result<void> updateSphereBatch(graphics::multiscale::SphereBatch& batch,
                                                 const GpuSphereProjection&         projection);

    /**
     * @brief Replace the complete per-target appearance array used by subsequent projections.
     * @param appearances One finite positive-radius appearance per target, in target order; copied before return.
     * @return Success or structured validation/backend failure; the previous array survives failure.
     * @ownership The session owns the GPU copy and retains no caller memory.
     * @thread Graphics/GPGPU owner thread only, outside a render pass; no callbacks.
     * @cost O(Nt) validation, upload and device storage; call when appearances change, not each frame.
     * @remarks Until called, updateSphereBatch uses GpuSphereProjection's uniform appearance.
     */
    [[nodiscard]] Result<void> setSphereAppearances(std::span<const GpuSphereAppearance> appearances);

    /**
     * @brief Atomically apply sorted distinct appearance edits to the published GPU array.
     * @param edits Strictly increasing target indices and finite appearances; borrowed only during the call.
     * @return Success or structured failure with the previous appearance array still published.
     * @ownership The session owns both device versions; no edit span or pointer is retained.
     * @thread Graphics/GPGPU owner thread only, outside a render pass; no callbacks.
     * @cost O(Nt) device-local copy plus O(K) host validation, staging and recorded uploads for K edits;
     * one GPU submission and synchronization. Reuses the spare buffer after the first edit batch.
     * @remarks Requires setSphereAppearances first; an empty batch succeeds without GPU work.
     */
    [[nodiscard]] Result<void> editSphereAppearances(std::span<const GpuSphereAppearanceEdit> edits);

    /**
     * @brief Explicitly read back the complete currently published source and target state.
     * @return Owning snapshot or backend/stale-provider failure.
     * @cost O(Ns+Nt) device-to-host transfer and output storage; do not call per frame.
     */
    [[nodiscard]] Result<GpuFieldSnapshot> snapshot() const;

    /** @brief Return the last published tick and revision without GPU work. */
    [[nodiscard]] FieldStamp stamp() const noexcept;

private:
    struct Impl;
    explicit GpuGravitySession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::physics::field::gpu
