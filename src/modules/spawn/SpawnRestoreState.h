#pragma once
/** @file SpawnRestoreState.h
 * @brief spawn module-interface: strict owning runtime admission, without live publication.
 * @remarks No providers, ECS entities, observers, script/tool endpoints or source leases are created.
 */
#include <memory>
#include <span>
#include "common/Snapshot.h"
#include "spawn/SpawnMap.h"
#include "spawn/SpawnSavedState.h"
namespace eve::spawn {
/** @brief Immutable canonical placement content at an admitted durable source address; no live authority.
 * @ownership Shares the exact per-source compiled map produced by snapshot admission; instance is its index.
 * The map is a validation carrier, not a replacement for the original map asset identity.
 */
struct SavedPlacementBinding {
    SavedSpawnerKey                    source;
    std::shared_ptr<const CompiledMap> map;
    std::size_t                        instance = 0;
};
class SpawnWorld;
class IUnitDefinitionResolver;
/** @brief Validated typed checkpoint facts for a later coordinated restore, not a running world.
 * @ownership Owns compiled pinned templates and typed scheduler data. Cancellation releases local memory.
 * @thread Owner-thread admission; callbacks receive no live world and execute without locks.
 */
class EVENGINE_API_FOUNDATION SpawnRestoreState {
public:
    /** @brief Release unpublished local data. */
    ~SpawnRestoreState();
    /** @brief Transfer admitted state. */
    SpawnRestoreState(SpawnRestoreState&&) noexcept;
    /** @brief Release the old candidate and transfer ownership. */
    SpawnRestoreState& operator=(SpawnRestoreState&&) noexcept;
    SpawnRestoreState(const SpawnRestoreState&)            = delete;
    SpawnRestoreState& operator=(const SpawnRestoreState&) = delete;
    /** @brief Verify envelope, exact revision sources, strict v1 fields and internal ledgers before admission.
     * @param snapshot Borrowed only for this call; copied before invoking external callbacks.
     * @param saveEpoch Required joint-save identity, checked against this participant.
     * @param hashProvider Required digest implementation; no default or latest-revision fallback.
     * @param units Resolver for the joint save's admitted unit archive; must not substitute current live content.
     * @return Owning typed data or failure. Neither outcome changes any running world.
     * @cost Full payload parsing, template compilation and index validation per restore candidate.
     */
    [[nodiscard]] static Result<SpawnRestoreState> decode(const SnapshotEnvelope& snapshot, PersistentId saveEpoch,
                                                          const SnapshotHashProvider&    hashProvider,
                                                          const IUnitDefinitionResolver& units);
    /** @brief Copy admitted CPU facts for an independent restoration attempt, retaining this candidate for retry.
     * @return Owning authority-free candidate, or consumed/in-progress state failure without changing this owner.
     * @ownership Copies scheduler arrays/ledgers and shares only immutable compiled revisions; no live providers,
     * leases, observers, entities or digest callbacks are copied or issued. Each successful bind gets fresh authority.
     * @thread Simulation/content owner thread; rejects reentry while provider binding is in progress, no callbacks.
     * @cost Full owning scheduler/ledger and projection copy per isolated restore attempt; immutable content is shared.
     * @throws std::bad_alloc Copy failed without source mutation; the source remains retryable.
     */
    [[nodiscard]] Result<SpawnRestoreState> clone() const;
    /** @brief Joint-save identity for participant coordination; no callbacks. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Saved world namespace for provider binding; nil after move/consumption, never live authority. */
    [[nodiscard]] PersistentId owner() const noexcept;
    /** @brief Verified participant revision; zero after move, no callbacks. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Verified simulation boundary; zero after move, no callbacks. */
    [[nodiscard]] SimulationTick tick() const noexcept;
    /** @brief Read the actual shared bound for player/event activation inputs; zero after move.
     * @ownership Value only; no callbacks, allocation or mutation. @thread Simulation/content owner thread. */
    [[nodiscard]] std::uint32_t activationInputLimit() const noexcept;
    /** @brief Last lifecycle sequence already consumed by the saved core; zero after move. */
    [[nodiscard]] std::uint64_t acceptedLifecycleSequence() const noexcept;
    /** @brief Borrow sorted admitted durable membership until move/destruction; empty after move.
     * @remarks No live source handles, leases, callbacks or allocation; useful only for joint relation admission.
     */
    [[nodiscard]] std::span<const SavedSpawnMember> membership() const noexcept;
    /** @brief Borrow durable addresses of all occupied sources in slot order until move/successful restore.
     * @remarks Includes empty sources; excludes retired tombstones. No live lease or callback is issued.
     */
    [[nodiscard]] std::span<const SavedSpawnerKey> sources() const noexcept;
    /** @brief Borrow canonical placement bindings for occupied slots, retaining original slot addresses.
     * @ownership Borrow valid until move/destruction or successful core restore; shared map copies may outlive it.
     * @remarks Retired slots are absent, never compacted into another source address. No callbacks or allocation.
     */
    [[nodiscard]] std::span<const SavedPlacementBinding> placementBindings() const noexcept;
    /** @brief Number of saved slots, including retired tombstones. */
    [[nodiscard]] std::size_t slotCount() const noexcept;
    /** @brief Number of tracked subjects across active slots. */
    [[nodiscard]] std::size_t trackedCount() const noexcept;

private:
    friend class SpawnWorld;
    struct Impl;
    explicit SpawnRestoreState(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn
