#pragma once
/** @file SpawnCheckpoint.h
 * @brief spawn module-interface: opaque owning checkpoint data and explicit digest sealing.
 * @remarks No ECS, capability registration, live providers, callbacks or subscriptions are retained.
 */
#include <memory>
#include <span>
#include "common/ResourceRef.h"
#include "common/Snapshot.h"
namespace eve::spawn {
class SpawnWorld;
class IUnitDefinitionResolver;
struct CheckpointSubject;
/** @brief Owning frozen core state; may outlive the source world and be sealed independently.
 * @ownership Owns copied checkpoint data only. Destruction performs no business action.
 * @thread Owner-thread sealing; same-token reentry is rejected, no locks are held in the hash callback.
 */
class EVENGINE_API_FOUNDATION SpawnCheckpoint {
public:
    /** @brief Release frozen local data. */
    ~SpawnCheckpoint();
    /** @brief Transfer frozen checkpoint ownership. */
    SpawnCheckpoint(SpawnCheckpoint&&) noexcept;
    /** @brief Release the old data and transfer ownership. */
    SpawnCheckpoint& operator=(SpawnCheckpoint&&) noexcept;
    SpawnCheckpoint(const SpawnCheckpoint&)            = delete;
    SpawnCheckpoint& operator=(const SpawnCheckpoint&) = delete;
    /** @brief Digest exact pinned revisions and seal the frozen eve.spawn-runtime/v1 envelope.
     * @param hashProvider Required injected digest implementation; never retained.
     * @return Owning sealed envelope or failure preserving this checkpoint for retry.
     * @remarks Source world changes/destruction cannot affect this frozen state. No live world is accessed.
     * Hash callbacks must not move/destroy this checkpoint.
     * @cost Payload copy, ordered digest validation and canonical JSON hashing per seal; amortize per save artifact.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hashProvider) const;
    /** @brief Admit a sealed checkpoint using the canonical core decoder, without creating live authority.
     * @param snapshot Owning file envelope, pinned before any digest or unit callback.
     * @param saveEpoch Required joint-save identity.
     * @param hashProvider Required injected digest implementation; never retained.
     * @param units Borrowed resolver for exact saved unit revisions; never retained.
     * @return Frozen checkpoint or failure without changing any running world.
     * @ownership Owns admitted payload and reference projections; source handles have zero incarnation.
     * @thread Calling content thread; callbacks run without locks and must not mutate the resolver.
     * @remarks Reuses strict schema/version, content and ledger admission. No providers, leases or entities exist.
     * @cost Full payload decoding, template compilation and reference indexing per file admission.
     */
    [[nodiscard]] static Result<SpawnCheckpoint> parse(SnapshotEnvelope snapshot, PersistentId saveEpoch,
                                                       const SnapshotHashProvider&    hashProvider,
                                                       const IUnitDefinitionResolver& units);
    /** @brief Borrow sorted unique unit references from all saved revisions, waves and retry intentions.
     * @ownership Detached owning references; borrow lasts until move/destruction. No callbacks or allocation.
     * @remarks The domain captures exact current recipes for these future references at the same save boundary.
     */
    [[nodiscard]] std::span<const DefinitionRef> unitReferences() const noexcept;
    /** @brief Borrow frozen subject/source associations until this checkpoint is moved or destroyed.
     * @remarks Owning observations, unaffected by source destruction; not restored handles or leases.
     * File-admitted checkpoints use zero process incarnation and only describe durable source coordinates.
     * The coordinator uses them to join domain facts before sealing. No callbacks or allocation.
     */
    [[nodiscard]] std::span<const CheckpointSubject> membership() const noexcept;

private:
    friend class SpawnWorld;
    struct Impl;
    explicit SpawnCheckpoint(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn
