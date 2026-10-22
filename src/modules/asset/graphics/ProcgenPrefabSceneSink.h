#pragma once
#include <memory>
#include "common/ProcgenSceneSink.h"

namespace eve::graphics {
class Graphics;
}
namespace eve::asset_graphics {
class EvpackStaticPrefab;
/** @brief Scene-sink decorator projecting exact-build procedural prefab bindings into owned ECS renderers.
 * @ownership Scene remains the author of batch metadata; cached snapshots are renderer projections guarded by its
 * revision.
 * @lifetime Graphics, underlying Scene sink, ECS table and admitted prefab factories outlive this object and release.
 * @thread Graphics/Scene/ECS owner thread, outside iteration; synchronous, non-reentrant, no locks.
 * @remarks Composition owns capability registration/unregistration and makes this the sole batch writer while
 * installed. Unbound asset labels remain metadata anchors. No implicit IO, prefab lookup by label, Scene links or
 * simulation System.
 * Admitted mesh/image leases must belong to the bound Graphics device/context; cross-context variant admission is
 * unsupported. Capacity bounds: 4096 admitted asset/build versions, 64 active/retired transaction batches, and 65536
 * peak instances/renderer parts across active, retired and prepared snapshots. Collect retired resources between large
 * publications.
 * Each delta collection is bounded to 65536 entries before staging.
 */
class EVENGINE_API_WORLD ProcgenPrefabSceneSink final : public IProcgenSceneSink {
public:
    /** @brief Bind borrowed rendering and Scene owners; does not install a global capability or publish anything. */
    ProcgenPrefabSceneSink(graphics::Graphics& graphics, IProcgenSceneSink& scene);
    /** @brief Attempt checked release before teardown and report failures; explicit release permits retry. */
    ~ProcgenPrefabSceneSink() override;
    ProcgenPrefabSceneSink(const ProcgenPrefabSceneSink&)            = delete;
    ProcgenPrefabSceneSink& operator=(const ProcgenPrefabSceneSink&) = delete;
    /** @brief Admit and pin one immutable asset/build version; historical versions remain available for undo/restore.
     * @return Success or null/released/duplicate-version/budget failure, without replacing existing versions.
     * @cost Logarithmic insertion plus a fixed pin allocation, at most 4096 versions after explicit loading; no
     * IO/upload.
     */
    [[nodiscard]] Result<void> admit(std::shared_ptr<EvpackStaticPrefab> prefab);
    /** @brief Release retired, already hidden renderers without changing committed batches.
     * @return Success or cleanup failure retaining resources for retry.
     * @cost Linear in retired renderer parts, amortized after explicit publication; no Scene callbacks.
     */
    [[nodiscard]] Result<void> collectRetired();
    /** @brief Remove all owned batches through Scene coordination, collect resources and close the decorator.
     * @return Success or checked publication/cleanup failure; retry before owner teardown.
     * @cost Linear in owned and retired parts plus Scene staging, once per owner teardown; no IO.
     */
    [[nodiscard]] Result<void> release();
    /** @brief Number of active renderer parts in a batch; metadata-only anchors contribute zero. */
    [[nodiscard]] std::size_t rendererCount(const std::string& batchId) const noexcept;
    /** @brief Number of retired batch resource sets awaiting collectRetired, bounded to 64. */
    [[nodiscard]] std::size_t pendingRetirements() const noexcept;
    /** @brief Compatibility projection assigning absent PointIds by stable instance id before canonical replacement.
     * @cost Same full-batch staging as replaceBatch, amortized per explicit publication.
     */
    bool applyBatch(const std::string&, const std::vector<ProcgenInstanceDesc>&) override;
    /** @copydoc IProcgenSceneSink::replaceBatch
     * @cost Linear in prepared renderer parts plus Scene staging, with allocations once per explicit publication.
     */
    [[nodiscard]] Result<uint64_t> replaceBatch(const std::string&, uint64_t,
                                                const std::vector<ProcgenInstanceDesc>&) override;
    /** @copydoc IProcgenSceneSink::replaceBatches
     * @cost Full staging of every affected batch and the Scene cache, amortized per explicit publication.
     */
    [[nodiscard]] Result<uint64_t> replaceBatches(const std::vector<ProcgenBatchSnapshot>&) override;
    /** @copydoc IProcgenSceneSink::replaceBatchesCoordinated
     * @cost Full renderer/Scene staging per explicit publication; the external owner must already be prepared.
     */
    [[nodiscard]] Result<uint64_t> replaceBatchesCoordinated(const std::vector<ProcgenBatchSnapshot>&,
                                                             const std::function<Result<void>()>&) override;
    /** @brief Compatibility wrapper over checked coordinated removal.
     * @cost Same Scene staging and renderer retirement as removeBatches, once per explicit removal.
     */
    bool removeBatch(const std::string&) override;
    /** @copydoc IProcgenSceneSink::removeBatches
     * @cost Linear in affected renderer parts plus Scene cache staging per explicit removal; cleanup is deferred.
     */
    [[nodiscard]] Result<uint64_t> removeBatches(const std::vector<std::string>&) override;
    /** @copydoc IProcgenSceneSink::removeBatchesCoordinated
     * @cost Scene cache staging and affected renderer retirement per explicit removal; the owner is pre-prepared.
     */
    [[nodiscard]] Result<uint64_t> removeBatchesCoordinated(const std::vector<std::string>&,
                                                            const std::function<Result<void>()>&) override;
    /** @copydoc IProcgenSceneSink::applyDelta
     * @cost Identity staging plus full affected-batch renderer preparation, amortized per explicit delta.
     * @remarks Scene identities persist; this adapter currently recreates affected ECS renderer entities.
     */
    [[nodiscard]] Result<uint64_t> applyDelta(const std::string&, const ProcgenInstanceDelta&) override;
    /** @copydoc IProcgenSceneSink::batchRevision */
    uint64_t batchRevision(const std::string&) const override;
    /** @copydoc IProcgenSceneSink::instanceCount */
    int instanceCount(const std::string&) const override;
    /** @copydoc IProcgenSceneSink::lastCreatedCount */
    int lastCreatedCount(const std::string&) const override;
    /** @copydoc IProcgenSceneSink::lastReusedCount */
    int lastReusedCount(const std::string&) const override;
    /** @copydoc IProcgenSceneSink::lastRemovedCount */
    int lastRemovedCount(const std::string&) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_graphics
