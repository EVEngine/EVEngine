#pragma once
/** @file RPGSpawnJointRestore.h
 * @brief rpg_spawn module-interface: admitted same-boundary core/domain/lifecycle relations before joint publication.
 * @remarks No entities, capabilities, runtime sources, leases, registries, callbacks or events are published.
 * This phase does not materialize or replace a running world.
 */
#include <memory>
#include <span>
#include "common/Snapshot.h"
#include "spawn/SpawnSavedState.h"
namespace eve::spawn {
class SpawnRestoreState;
class SpawnWorld;
struct SpawnProviders;
}  // namespace eve::spawn
namespace eve::rpg {
class SkillConditionBindings;
}
namespace eve::rpg_spawn {
class RPGSpawnRestoreState;
/** @brief Opaque owning joined restoration facts, with all currently tracked subjects explained.
 * @ownership Independent of caller envelopes, providers and source worlds. Owns admitted participants and pending data.
 * @thread Simulation/content owner thread; only the explicit digest provider invokes external code, without locks.
 */
class EVENGINE_API_PLATFORM RPGSpawnJointRestore {
public:
    /** @brief Release unpublished facts without business cleanup. */
    ~RPGSpawnJointRestore();
    /** @brief Transfer joined facts. */
    RPGSpawnJointRestore(RPGSpawnJointRestore&&) noexcept;
    /** @brief Cancel old facts and transfer joined ownership. */
    RPGSpawnJointRestore& operator=(RPGSpawnJointRestore&&) noexcept;
    RPGSpawnJointRestore(const RPGSpawnJointRestore&)            = delete;
    RPGSpawnJointRestore& operator=(const RPGSpawnJointRestore&) = delete;
    /** @brief Admit both snapshots and their unconsumed lifecycle relations without live content substitution.
     * @param core Core envelope, copied before any digest callback.
     * @param domain RPG envelope, also copied before the first digest callback, not after core verification.
     * @param saveEpoch Required common save identity; verified participant revision and tick must also agree.
     * @param hashProvider Required synchronous digest implementation, never retained.
     * @param pending Durable lifecycle facts copied before callbacks; order must exceed the core's consumed sequence.
     * @param conditions Optional actual immutable policy owner shared into domain candidates, not invoked or certified
     * by preparation. Call domain().validateImplementationBindings before materialization.
     * @return Owning joined state or failure without changing inputs or any live owner.
     * @remarks In-memory semantic coordination only; persisted artifacts use prepareBundle to cover pending integrity.
     * Units resolve only through explicit future bindings in the verified RPG archive; multiple historical
     * actor generations are allowed. Missing bindings, including legacy actor-only archives, never select a generation
     * by name or by newest record. The selected immutable recipes are retained in domain(), but are not installed in
     * a running factory by this phase. Pending-participant integrity and materialization/publication still require the
     * coordinator's remaining admission stages.
     * Missing tracked actors require matching pending removals. Present pending actors must have cleared origins;
     * other tracked actors require matching origins and positive hp. Untracked retained actors must have no origin.
     * @cost Copies/hash-verifies both payloads, compiles canonical content and indexes all relations per restore
     * attempt.
     * @throws std::bad_alloc Local allocation failed without publication or input mutation.
     */
    [[nodiscard]] static Result<RPGSpawnJointRestore> prepare(
        const SnapshotEnvelope& core, const SnapshotEnvelope& domain, PersistentId saveEpoch,
        const SnapshotHashProvider& hashProvider, std::span<const spawn::SavedLifecycleInput> pending = {},
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Seal core/domain envelopes and pending inputs as one integrity-covered eve.rpg-spawn-bundle/v1.
     * @ownership Copies every input before the first hash callback; output owns nested envelopes and durable events.
     * @return Sealed bundle after existing joint semantic admission, or failure without input/live mutation.
     * @remarks Strict v1 fields, no legacy pending inference. Hash algorithm/security is caller-defined.
     * This covers Spawn participants, not map assets, full RPG active state or atomic world publication.
     * @thread Simulation/content owner thread, no locks; only the synchronous hash provider executes callbacks.
     * @cost Full joint admission, participant copying and canonical bundle hashing per save artifact.
     * @throws std::bad_alloc Local allocation failed without publication.
     */
    [[nodiscard]] static Result<SnapshotEnvelope> sealBundle(const SnapshotEnvelope& core,
                                                             const SnapshotEnvelope& domain, PersistentId saveEpoch,
                                                             std::span<const spawn::SavedLifecycleInput> pending,
                                                             const SnapshotHashProvider&                 hashProvider);
    /** @brief Verify a whole v1 bundle, then admit its same-boundary participants and pending relations.
     * @ownership Copies the complete input before any callback; result owns all admitted facts independently.
     * @return Joined candidate or digest/schema/version/relation failure without publication or live lookup.
     * @remarks Unknown fields/versions reject; pending cannot be supplied or overridden outside the bundle.
     * @param conditions Actual immutable executors shared into candidates; never invoked by admission.
     * @thread Simulation/content owner thread; only the injected digest provider executes external code, without locks.
     * @cost Whole bundle verification, nested envelope verification and canonical joint admission per restore attempt.
     * @throws std::bad_alloc Local allocation failed without source/world mutation.
     */
    [[nodiscard]] static Result<RPGSpawnJointRestore> prepareBundle(
        const SnapshotEnvelope& bundle, PersistentId saveEpoch, const SnapshotHashProvider& hashProvider,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Borrow admitted core until move/destruction; requires an unconsumed object. No callbacks/allocation. */
    [[nodiscard]] const spawn::SpawnRestoreState& core() const noexcept;
    /** @brief Borrow admitted domain until move/destruction; requires an unconsumed object. No callbacks/allocation. */
    [[nodiscard]] const RPGSpawnRestoreState& domain() const noexcept;
    /** @brief Share the exact admitted immutable domain with its factory; empty after move.
     * @ownership Owner survives this joint candidate; no copying, callbacks or registry lookup.
     */
    [[nodiscard]] std::shared_ptr<const RPGSpawnRestoreState> domainOwner() const noexcept;
    /** @brief Borrow copied durable lifecycle inputs until move/destruction; empty after move. */
    [[nodiscard]] std::span<const spawn::SavedLifecycleInput> pendingLifecycle() const noexcept;
    /** @brief Joint save identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;

private:
    friend class PreparedRPGSpawnSession;
    [[nodiscard]] Result<spawn::SpawnWorld> bindCore(const std::shared_ptr<spawn::SpawnProviders>& providers);
    struct Impl;
    explicit RPGSpawnJointRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rpg_spawn
