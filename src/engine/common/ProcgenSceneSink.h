#pragma once

#include <optional>
#include "common/Export.h"
#include "common/PrefabRootPose.h"
#include "common/ResourceRef.h"
#include "common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve {
/** @brief Stable procedural prefab link at an exact admitted package build; contains no runtime pointers. */
struct ProcgenInstanceAssetBinding {
    AssetRef       asset;
    PersistentId   buildId;
    PrefabRootPose rootPose =
        PrefabRootPose::Authored;  ///< Missing historical PointSet attributes retain authored pose.
};

/** @brief Engine-neutral description of one procedural scene instance. */
struct EVENGINE_API_FOUNDATION_INLINE ProcgenInstanceDesc {
    /** @brief Stable source PointId; zero denotes a legacy instance without delta identity. */
    uint64_t    sourcePointId = 0;
    std::string id;
    /** @brief Compatibility asset label; when assetBinding exists, its typed identity is authoritative and must match.
     */
    std::string asset;
    float       x      = 0.f;
    float       y      = 0.f;
    float       z      = 0.f;
    /** @brief Local Y rotation in degrees; the scene provider converts all Euler angles to its native radians. */
    float       yaw    = 0.f;
    float       scaleX = 1.f;
    float       scaleY = 1.f;
    float       scaleZ = 1.f;
    uint32_t    seed   = 1;
    /** @brief Local X rotation in degrees, composed as yaw(Y), pitch(X), roll(Z); zero preserves legacy instances. */
    float pitch = 0.f;
    /** @brief Local Z rotation in degrees; owned value retained by full snapshots and identity-based deltas. */
    float roll = 0.f;
    /** @brief Canonical asset link; asset remains its matching compatibility label when present. */
    std::optional<ProcgenInstanceAssetBinding> assetBinding{};
};

/** @brief Identity-based, revision-checked mutation of one procedural scene batch. */
struct EVENGINE_API_FOUNDATION_INLINE ProcgenInstanceDelta {
    uint64_t                         baseRevision   = 0;
    uint64_t                         targetRevision = 0;
    std::vector<ProcgenInstanceDesc> added;
    std::vector<ProcgenInstanceDesc> updated;
    /** @brief Stable source PointIds removed from the target batch. */
    std::vector<uint64_t> removedPointIds;
    /** @brief Compatibility removals addressed directly by Scene instance id. */
    std::vector<std::string> removed;
    /** @brief Exact target order addressed by stable source PointId. */
    std::vector<uint64_t> targetPointOrder;
    /** @brief Compatibility target order addressed directly by Scene instance id. */
    std::vector<std::string> targetOrder;
};

/** @brief Build an owning target snapshot from one exact revision and identity delta; does not publish state.
 * @param current Borrowed complete committed snapshot, retained only during this call.
 * @param currentRevision Non-zero revision matching delta.baseRevision.
 * @param delta Borrowed update, including exact target order and the next non-zero revision.
 * @return Complete detached target or identity/revision failure; inputs remain unchanged.
 * @cost Linear expected time and owning staging memory in current plus changed instances; amortized per explicit delta.
 * @thread Any thread with immutable inputs; synchronous, no callbacks, global services or RNG.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<ProcgenInstanceDesc>> buildProcgenInstanceDeltaSnapshot(
    const std::vector<ProcgenInstanceDesc>& current, uint64_t currentRevision, const ProcgenInstanceDelta& delta);

/** @brief One complete cell snapshot participating in an atomic multi-batch publication. */
struct EVENGINE_API_FOUNDATION_INLINE ProcgenBatchSnapshot {
    /** @brief Stable target batch identity. */
    std::string batchId;
    /** @brief Non-zero revision newer than the committed batch revision. */
    uint64_t targetRevision = 0;
    /** @brief Complete ordered target contents. */
    std::vector<ProcgenInstanceDesc> instances;
};

/**
 * @brief Optional scene consumer for procedural instance batches.
 *
 * The interface lives in common so procgen can publish results without an
 * upward dependency on scene. Scene providers must replace a batch atomically
 * and preserve stable instance ids across rebuilds.
 */
class EVENGINE_API_FOUNDATION_INLINE IProcgenSceneSink {
public:
    static constexpr const char* capabilityName = "IProcgenSceneSink";

    /** @brief I procgen scene sink. */
    virtual ~IProcgenSceneSink() = default;
    /** @brief Create or reconcile all instances in a named batch. */
    virtual bool applyBatch(const std::string& batchId,
                            const std::vector<ProcgenInstanceDesc>& instances) = 0;
    /**
     * @brief Atomically replace a batch with a complete stable-identity snapshot at an explicit revision.
     * @param batchId Non-empty batch identity.
     * @param targetRevision Non-zero revision newer than the currently committed batch revision.
     * @param instances Complete ordered target snapshot; every instance must have a unique non-zero source PointId.
     * @return The committed target revision, or a structured stale/validation/provider failure.
     * @thread Called synchronously on the scene-owning thread; implementations must not retain references.
     * @reentrant Not reentrant for the same batch.
     */
    [[nodiscard]] virtual Result<uint64_t> replaceBatch(
        const std::string& batchId, uint64_t targetRevision,
        const std::vector<ProcgenInstanceDesc>& instances) = 0;
    /**
     * @brief Atomically replace several batches from complete snapshots.
     * @param snapshots Non-empty transaction whose batch ids are unique.
     * @return Number of committed batches, or a structured failure with no batch changed.
     * @thread Called synchronously on the scene-owning thread; implementations must not retain references.
     * @reentrant Not reentrant. No scene callback may observe a partially committed transaction.
     */
    [[nodiscard]] virtual Result<uint64_t> replaceBatches(const std::vector<ProcgenBatchSnapshot>& snapshots) = 0;
    /** @brief Remove the visible contents of a batch. */
    virtual bool removeBatch(const std::string& batchId) = 0;
    /**
     * @brief Atomically remove several published batches.
     * @param batchIds Non-empty unique identities of existing batches.
     * @return Number of removed batches, or a structured failure with no batch changed.
     * @thread Called synchronously on the scene-owning thread; implementations must not retain references.
     * @reentrant Not reentrant. Callbacks run only after every batch is no longer visible or queryable.
     */
    [[nodiscard]] virtual Result<uint64_t> removeBatches(const std::vector<std::string>& batchIds) = 0;
    /**
     * @brief Remove several batches while coordinating one external authoritative commit.
     * @param batchIds Non-empty unique identities of existing batches.
     * @param commitPreparedOwner Called after Scene preparation and before Scene state becomes visible.
     * @return Number of removed batches, or a structured failure with neither owner changed.
     * Post-commit notification exceptions return Applied with warning diagnostics; they do not undo publication.
     * @thread Called synchronously on the scene-owning thread; references are not retained.
     * @reentrant The commit callback must not reenter Scene and must leave its owner unchanged on failure.
     */
    [[nodiscard]] virtual Result<uint64_t> removeBatchesCoordinated(
        const std::vector<std::string>& batchIds, const std::function<Result<void>()>& commitPreparedOwner) = 0;
    /**
     * @brief Apply one prevalidated identity delta atomically to an existing batch.
     * @return The committed target revision, or a structured stale/validation/provider failure.
     * @thread Called synchronously on the scene-owning thread; implementations must not retain references.
     * @reentrant Not reentrant for the same batch.
     */
    [[nodiscard]] virtual Result<uint64_t> applyDelta(const std::string& batchId,
                                                       const ProcgenInstanceDelta& delta) = 0;
    /** @brief Current committed revision of a batch, or zero when it is absent. */
    virtual uint64_t batchRevision(const std::string& batchId) const = 0;
    /** @brief Number of currently published instances in a batch. */
    virtual int instanceCount(const std::string& batchId) const = 0;
    /** @brief Instances newly created by the batch's latest successful apply. */
    virtual int lastCreatedCount(const std::string& batchId) const = 0;
    /** @brief Stable-id instances reused by the batch's latest successful apply. */
    virtual int lastReusedCount(const std::string& batchId) const = 0;
    /** @brief Instances removed by the batch's latest successful apply or removal. */
    virtual int lastRemovedCount(const std::string& batchId) const = 0;
    /** @brief Replace complete batches while committing one prepared external resource owner.
     * @param snapshots Non-empty unique batch snapshots with newer non-zero revisions.
     * @param commitPreparedOwner Synchronous commit after Scene preparation, before any Scene publication or events.
     * @return Committed batch count, or failure with prior Scene batches unchanged. Unsupported providers do not call
     * commit.
     * Post-commit notification exceptions return Applied with warning diagnostics, preserving the committed outcome.
     * @thread Scene-owner thread, outside ECS iteration; references are call-scoped, no locks.
     * @reentrant Commit must not access or mutate Scene, invoke scripts, or reenter the sink; failure leaves its owner
     * unchanged.
     * @cost Linear in target instances and the committed Scene cache; staging allocations amortized per explicit
     * publication.
     */
    [[nodiscard]] virtual Result<uint64_t> replaceBatchesCoordinated(
        const std::vector<ProcgenBatchSnapshot>& snapshots, const std::function<Result<void>()>& commitPreparedOwner) {
        (void)snapshots;
        (void)commitPreparedOwner;
        return Result<uint64_t>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "scene provider does not support coordinated batch replacement"));
    }
};

}  // namespace eve
