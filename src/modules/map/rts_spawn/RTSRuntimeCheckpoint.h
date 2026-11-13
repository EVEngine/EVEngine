#pragma once
/** @file RTSRuntimeCheckpoint.h
 * @brief map_rts_spawn module-interface: owning-runtime joint freeze, including clock/handoff responsibility.
 * @remarks Native closed-profile persistence; joined admission and candidate publication remain separate.
 */
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
namespace eve::definitions {
class DefinitionRegistryCapture;
}
namespace eve::combat {
class DamageCapture;
}
namespace eve::crowd {
struct AgentFrame;
}
namespace eve::map {
class PathfinderState;
}
namespace eve::rts {
class RTSCrowdCapture;
class RTSNavigationCapture;
class RTSUnitAttributeCapture;
class RTSWeaponCapture;
class RTSActionCapture;
struct RTSFrameState;
struct RTSCombatSnapshot;
}  // namespace eve::rts
namespace eve::sensing {
class SensingCapture;
}
namespace eve::map_rts_spawn {
class RTSMapRuntime;
class RTSMapCheckpoint;
/** @brief Owning runtime's captured handoff phase; an executing/faulted/closing runtime cannot freeze. */
enum class RTSCheckpointPhase { Ready, SpawnPending };
/** @brief Stable domain identity joined to one row of this checkpoint's immutable Crowd frame.
 * @remarks frameIndex is an in-memory row correspondence, never a durable agent identity.
 */
struct RTSCrowdCheckpointMember {
    SubjectRef  subject;
    std::size_t frameIndex = 0;
};
/** @brief Detached participants frozen by their actual game owner at one synchronous boundary.
 * @ownership Owns map/core/provenance/roots, Crowd, actions/correlations, events and sensing/combat facts;
 * retains no source owner.
 * @thread Capture on owner thread outside Views; inspection never advances simulation or invokes services.
 * @remarks Persistent sealing supports the explicitly closed native context only. External callbacks/providers
 * remain rejected; parsing must be followed by joined admission and isolated candidate publication.
 */
class EVENGINE_API_ORCHESTRATION RTSRuntimeCheckpoint final {
public:
    /** @brief Release detached facts only. */
    ~RTSRuntimeCheckpoint();
    /** @brief Transfer detached facts. */
    RTSRuntimeCheckpoint(RTSRuntimeCheckpoint&&) noexcept;
    /** @brief Replace detached facts. */
    RTSRuntimeCheckpoint& operator=(RTSRuntimeCheckpoint&&) noexcept;
    RTSRuntimeCheckpoint(const RTSRuntimeCheckpoint&)            = delete;
    RTSRuntimeCheckpoint& operator=(const RTSRuntimeCheckpoint&) = delete;
    /** @brief Seal all closed-native participants as eve.map-rts-runtime / map:rts-runtime v1.
     * @param hash Required injected digest; never retained.
     * @return Owning file envelope or context/digest/reentry failure, preserving facts for retry.
     * @ownership Pins all facts before callbacks, which may retire this source checkpoint.
     * @thread Quiescent content owner thread; same-source sealing reentry is rejected, no locks held.
     * @cost Full participant encoding and hashing per explicit game save.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hash) const;
    /** @brief Read strict closed-native participants without creating a world, provider or source authority.
     * @param snapshot Owning file envelope pinned before digest callbacks.
     * @param epoch Required joint-save identity. @param hash Required injected verifier; never retained.
     * @return Detached checkpoint or schema, metadata, content or participant failure.
     * @remarks Initial v1 rejects unknown fields/versions. Use PreparedRTSRuntimeRestore for joined admission
     * and complete isolated-world publication; parsing alone does not authorize gameplay restoration.
     * @thread Content owner thread; only digest callbacks execute, without locks or live services.
     * @cost Complete participant parsing and canonical core compilation per explicit game file load.
     */
    [[nodiscard]] static Result<RTSRuntimeCheckpoint> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                            const SnapshotHashProvider& hash);
    /** @brief Save epoch, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Joint revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Inspect the frozen native context verdict, without consulting current services.
     * @return Applied only for the captured closed native profile; moved or unadmitted context returns diagnostics.
     * @remarks Fact capture remains useful for inspection when callbacks/providers are not admitted for restoration.
     * This verdict does not alone authorize restoration: all joined participants and clocks require preparation.
     */
    [[nodiscard]] Result<void> validateRestoreContext() const;
    /** @brief Last completely handed-off frame, or zero after move. */
    [[nodiscard]] SimulationTick completedTick() const noexcept;
    /** @brief Phase at freeze, or empty after move. */
    [[nodiscard]] std::optional<RTSCheckpointPhase> phase() const noexcept;
    /** @brief Exact domain frame awaiting spawn handoff; absence means Ready (or moved). */
    [[nodiscard]] std::optional<SimulationStep> pendingStep() const noexcept;
    /** @brief Borrow map/core/source/native root facts until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const RTSMapCheckpoint>> map() const noexcept;
    /** @brief Borrow actual damage-rule/policy facts until checkpoint move/destruction; empty after move.
     * @remarks Native only; external damage-rule implementations require separate explicit admission.
     */
    [[nodiscard]] OptionalRef<const combat::DamageCapture> damage() const noexcept;
    /** @brief Borrow actual bound registry content, generations/history and envelope metadata; empty after move.
     * @ownership Borrow lasts until checkpoint move/destruction; source registry may already be gone.
     * @remarks Native only; does not by itself admit complete content references or external implementation identity.
     */
    [[nodiscard]] OptionalRef<const definitions::DefinitionRegistryCapture> definitions() const noexcept;
    /** @brief Borrow complete native Unit projections until move/destruction, or empty after move.
     * @remarks Includes non-combat attributes and exact native counters, without source owner references.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSUnitAttributeCapture>> unitAttributes()
        const noexcept;
    /** @brief Borrow all owned Unit/Building weapon relationships until move/destruction, or empty after move.
     * @remarks Includes non-generated bodies; no source-world handle or provider retained.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSWeaponCapture>> weapons() const noexcept;
    /** @brief Borrow native action and order correlations until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSActionCapture>> actions() const noexcept;
    /** @brief Borrow native event queues/cursors until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSFrameState>> events() const noexcept;
    /** @brief Borrow detached authoritative sensing facts/index policy until move/destruction. */
    [[nodiscard]] std::optional<std::reference_wrapper<const sensing::SensingCapture>> sensing() const noexcept;
    /** @brief Borrow combat mirror ownership and blocked-event latches until move/destruction. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSCombatSnapshot>> combat() const noexcept;
    /** @brief Borrow native backend and complete Unit bindings until move/destruction; empty after move.
     * @remarks External navigation jobs and provider identity still require separate joint admission. */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSCrowdCapture>> crowd() const noexcept;
    /** @brief Borrow detached actual map-provider caches, revisions and pending synchronization facts.
     * @ownership Borrow lasts until checkpoint move/destruction; the source map/provider may already be gone.
     * @remarks Native only; observes the provider owned by navigation(), without a second captured state.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const map::PathfinderState>> navigationProvider() const noexcept;
    /** @brief Borrow native provider and RTS surface-tracker facts until checkpoint move/destruction.
     * @remarks Does not authorize restoration of unmatched Crowd, roots or clocks; game joins them together.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const rts::RTSNavigationCapture>> navigation() const noexcept;
    /** @brief Borrow canonical Crowd observations (including process-local refs) until move/destruction. */
    [[nodiscard]] std::optional<std::reference_wrapper<const crowd::AgentFrame>> crowdFrame() const noexcept;
    /** @brief Borrow stable body-to-frame correspondences until move/destruction; empty after move. */
    [[nodiscard]] std::span<const RTSCrowdCheckpointMember> crowdMembers() const noexcept;

private:
    friend class RTSMapRuntime;
    struct Impl;
    explicit RTSRuntimeCheckpoint(std::unique_ptr<Impl> impl);
    std::shared_ptr<const Impl> impl_;
};
}  // namespace eve::map_rts_spawn
