#pragma once
/** @file RTSRuntimeHandoff.h
 * @brief map_rts_spawn module-interface: persistent phase/clock responsibility and ordered unconsumed inputs.
 * @remarks No world, provider, incarnation, acknowledgement ledger or restore authorization is created.
 */
#include <memory>
#include <optional>
#include <span>
#include "common/Snapshot.h"
namespace eve::spawn {
struct SavedLifecycleInput;
}
namespace eve::rts {
struct RTSFrameDamageEvent;
}
namespace eve::map_rts_spawn {
class RTSRuntimeCheckpoint;
enum class RTSCheckpointPhase;
/** @brief Immutable handoff facts copied from the actual owning runtime's checkpoint.
 * @ownership Owns CPU values only, without source world, source lease, callback or live handle.
 * @remarks Core and provenance remain the authoritative acknowledgement owners; the game coordinator must
 * validate these inputs against their same-boundary membership, sequences and pending consumer facts.
 */
class EVENGINE_API_ORCHESTRATION RTSRuntimeHandoff final {
public:
    /** @brief Release CPU facts. */
    ~RTSRuntimeHandoff();
    /** @brief Transfer CPU facts. */
    RTSRuntimeHandoff(RTSRuntimeHandoff&&) noexcept;
    /** @brief Replace CPU facts. */
    RTSRuntimeHandoff& operator=(RTSRuntimeHandoff&&) noexcept;
    RTSRuntimeHandoff(const RTSRuntimeHandoff&)            = delete;
    RTSRuntimeHandoff& operator=(const RTSRuntimeHandoff&) = delete;
    /** @brief Freeze the actual phase, pending step and both retained input stages without consuming them.
     * @return Owning archive or incoherent/moved checkpoint, frame or input/audit failure.
     * @ownership Synchronous borrow only. @thread Quiescent owner thread, no callbacks or services.
     * @cost Copies and validates every retained input/audit per explicit game checkpoint.
     * @throws std::bad_alloc Source and live games remain unchanged.
     */
    [[nodiscard]] static Result<RTSRuntimeHandoff> capture(const RTSRuntimeCheckpoint& checkpoint);
    /** @brief Seal eve.map-rts-handoff / map:rts-handoff v1 using the captured epoch/revision/domain tick.
     * @return Owning envelope or moved/digest failure; original order and duplicate inputs remain intact.
     * @remarks Facts are pinned before hash callbacks, which may retire the source archive.
     * @thread CPU save only; no locks, providers, world mutation or event delivery.
     * @cost Encodes and hashes all retained inputs/audits once per explicit persistent save.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hash) const;
    /** @brief Admit strict phase/clock and input fields without binding sources or acknowledging anything.
     * @param snapshot Owned envelope. @param epoch Expected nonnil joint identity. @param hash Explicit verifier.
     * @return Detached facts or unknown version/field, hash, phase/clock and input/audit failure.
     * @remarks Initial v1 has no migration. Whole-game relation admission and candidate publication remain separate.
     * @thread CPU admission; only the injected digest executes callbacks, without locks or services.
     * @cost Validates/copies complete retained inputs and audits per explicit file load.
     * @throws std::bad_alloc Input and live games remain unchanged.
     */
    [[nodiscard]] static Result<RTSRuntimeHandoff> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                         const SnapshotHashProvider& hash);
    /** @brief Save identity, nil after move; no callbacks. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Joint revision, zero after move; no callbacks. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Last fully consumed spawn frame, zero after move; no callbacks. */
    [[nodiscard]] SimulationTick completedTick() const noexcept;
    /** @brief Captured owner phase, empty after move; no callbacks. */
    [[nodiscard]] std::optional<RTSCheckpointPhase> phase() const noexcept;
    /** @brief Domain step awaiting spawn handoff, empty for Ready or after move; no callbacks. */
    [[nodiscard]] std::optional<SimulationStep> pendingStep() const noexcept;
    /** @brief Borrow ordered, possibly repeated untranslated observations until move/destruction. */
    [[nodiscard]] std::span<const rts::RTSFrameDamageEvent> damageInputs() const noexcept;
    /** @brief Borrow ordered durable translated facts until move/destruction; no live source handles issued. */
    [[nodiscard]] std::span<const spawn::SavedLifecycleInput> lifecycleInputs() const noexcept;

private:
    struct Data;
    explicit RTSRuntimeHandoff(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::map_rts_spawn
