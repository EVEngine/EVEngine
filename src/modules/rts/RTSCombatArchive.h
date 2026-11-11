#pragma once
/** @file RTSCombatArchive.h
 * @brief rts module-interface: versioned mirror ownership and FireBlocked latches, without world authority.
 * @remarks Sensing facts, body roots and provider identities remain separately owned joined participants.
 */
#include <memory>
#include "common/BorrowedRef.h"
#include "common/Snapshot.h"
namespace eve::rts {
struct RTSCombatSnapshot;
/** @brief Immutable combat state participant independent of all source and target world lifetimes. */
class EVENGINE_API_DOMAINS RTSCombatArchive final {
public:
    /** @brief Release immutable facts. */
    ~RTSCombatArchive();
    /** @brief Transfer facts. */
    RTSCombatArchive(RTSCombatArchive&&) noexcept;
    /** @brief Replace facts. */
    RTSCombatArchive& operator=(RTSCombatArchive&&) noexcept;
    RTSCombatArchive(const RTSCombatArchive&)            = delete;
    RTSCombatArchive& operator=(const RTSCombatArchive&) = delete;
    /** @brief Freeze canonical nonnil mirror identities and their blocked subset, without consulting a world.
     * @param state Owned copy of actual captured combat state.
     * @param epoch Nonnil joint save identity.
     * @param revision Nonzero joint revision.
     * @param tick Injected joint domain boundary.
     * @return Participant or state/identity diagnostics; no target or source is mutated.
     * @ownership Owns pure CPU sets only; no roots, providers, listeners or callbacks retained.
     * @thread Simulation/content owner outside world operations; no callbacks.
     * @cost Copies and validates mirror-sized sets per explicit capture.
     */
    [[nodiscard]] static Result<RTSCombatArchive> capture(RTSCombatSnapshot state, PersistentId epoch,
                                                          Revision revision, SimulationTick tick);
    /** @brief Seal rts:combat-state v1 with facts pinned before digest callbacks.
     * @param hash Explicit digest borrowed only during this call.
     * @return Envelope or moved/digest failure, preserving facts for retry.
     * @cost Encodes and hashes mirror-sized sets per explicit persistent sealing.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hash) const;
    /** @brief Verify an owning envelope and admit strict sets without resolving or installing mirrors.
     * @param snapshot Owned input copied before digest callbacks.
     * @param epoch Expected joint save identity; game coordinator separately checks revision/tick and participants.
     * @param hash Explicit digest borrowed during verification only.
     * @return Pure CPU participant or integrity/version/field/state diagnostics, without world publication.
     * @remarks Unknown fields/versions and duplicate/invalid identities reject; no implicit migration.
     * PreparedRTSCombatRestore still checks actual bodies, sensing facts and physical provider lifetimes.
     * @cost Parses and validates mirror-sized sets per explicit admission.
     */
    [[nodiscard]] static Result<RTSCombatArchive> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                        const SnapshotHashProvider& hash);
    /** @brief Borrow immutable state until archive move/destruction; empty after move. */
    [[nodiscard]] OptionalRef<const RTSCombatSnapshot> state() const noexcept;
    /** @brief Joint identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Joint revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Joint boundary, or zero after move. */
    [[nodiscard]] SimulationTick tick() const noexcept;

private:
    struct Data;
    explicit RTSCombatArchive(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::rts
