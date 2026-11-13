#pragma once
/** @file RPGSpawnCapture.h
 * @brief rpg_spawn module-interface: detached domain facts for coordinated saving, without live publication.
 * @remarks Reads RPGActor/SpawnedActorState and canonical skill records; no capability, events, scripts or tools.
 * This is an in-memory freeze, not a persistent format or a complete game save.
 */
#include "common/Snapshot.h"
#include "rpg/spawn/RPGSpawnFactory.h"
namespace eve::definitions {
struct Definition;
}
namespace eve::rpg {
struct EffectDefinition;
class SkillConditionBindings;
struct ImplementationIdentity;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
/** @brief Frozen spawned actor, including actors whose origin was cleared after death or KeepUnits.
 * @remarks Origin is an observed runtime identity for the coordinator's join, never a serialized lease.
 * The immutable recipe and canonical actor checkpoint own their data independently of the source world.
 */
struct CapturedSpawnActor {
    SubjectRef                          subject;
    std::optional<spawn::SpawnerHandle> origin;
    spawn::Point                        position;
    std::shared_ptr<const UnitRecipe>   recipe;
    std::string                         checkpointJson;
};
/** @brief Move-only frozen RPG spawn participant, including retained actors outside factory indexes.
 * @ownership Owns detached facts and exact skill records; never retains the source Table, factory or registry.
 * @thread Simulation owner thread; synchronous capture outside external Views, without callbacks.
 */
class EVENGINE_API_PLATFORM RPGSpawnCapture {
public:
    /** @brief Release frozen facts only. */
    ~RPGSpawnCapture();
    /** @brief Transfer frozen facts. */
    RPGSpawnCapture(RPGSpawnCapture&&) noexcept;
    /** @brief Release old frozen facts and transfer ownership. */
    RPGSpawnCapture& operator=(RPGSpawnCapture&&) noexcept;
    RPGSpawnCapture(const RPGSpawnCapture&)            = delete;
    RPGSpawnCapture& operator=(const RPGSpawnCapture&) = delete;
    /** @brief Freeze every RPGActor carrying SpawnedActorState in the given Table, ordered by persistent identity.
     * @param table Synchronous borrow; all spawned actors, including dead/kept actors, are included.
     * @param saveEpoch Non-nil joint save identity assigned by the caller at its quiescent simulation boundary.
     * @return Owning facts or failure with no source mutation. Missing provenance/content and unsafe actor
     * checkpoints fail explicitly; no latest unit recipe substitution or implicit lifecycle reconciliation.
     * @remarks Reuses RPGActor v5: complete attributes, active effects, cast clocks and execution order are captured.
     * Spawned actor keys must be present, non-nil and unique; older v1-v4 imports use a declared stable default. Live
     * other-actor targets require participant SpawnedActorState and registered rpg::ActorIdentity in the same
     * participant; targets outside it fail explicitly. Native handles are checked at capture and replaced with stable
     * identities; retired targets never rebind. Skill records, directly granted effects and every actually active
     * effect are frozen; actual versioned effect stack/attribute bindings and each actor's actual immutable condition
     * owner revisions are frozen. Missing, unversioned or mismatched per-use contracts fail; same named policy
     * revisions must agree across relevant actors. The coordinator still owns core/domain membership validation and
     * undelivered lifecycle/gameplay events. Actors without SpawnedActorState belong to other domain participants and
     * are not captured here.
     * @cost Full actor checkpoint encoding plus deduplicated skill record parsing/copying per save participant.
     * @throws std::bad_alloc Local allocation failed without publication or source mutation.
     */
    [[nodiscard]] static Result<RPGSpawnCapture> capture(ecs::Table& table, PersistentId saveEpoch);
    /** @brief Seal actor facts, exact unit/skill records and direct effect fields as eve.rpg-spawn-archive/v4.
     * @param revision Nonzero coordinator revision; tick is the shared simulation boundary.
     * @param hashProvider Required explicit digest implementation, not retained.
     * @return Owning envelope or failure preserving this capture for retry; no live state is accessed.
     * @remarks Runtime source incarnation and leases are never serialized. Unknown archive fields/versions are
     * rejected on import. Actor v1/v2 migrations and strict v3-v5 state use their canonical policies. Callback reentry
     * is rejected; callbacks must not move or destroy this capture. Provider exceptions become CallbackFailure.
     * @cost Copies and deduplicates all frozen facts, canonical JSON encoding and hashing per save participant.
     * @throws std::bad_alloc Local preparation failed without publication or source mutation.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hashProvider) const;
    /** @brief Borrow sorted granted and active effect facts until this capture is moved or destroyed. */
    [[nodiscard]] std::span<const rpg::EffectDefinition> effects() const noexcept;
    /** @brief Borrow frozen actual execution revisions until move/destruction; no callbacks or live lookup. */
    [[nodiscard]] std::span<const rpg::ImplementationIdentity> implementations() const noexcept;
    /** @brief Borrow sorted exact future unit source records until move/destruction; no live registry retained. */
    [[nodiscard]] std::span<const definitions::Definition> futureUnits() const noexcept;
    /** @brief Joint save identity, or nil after move; no callbacks. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow sorted frozen actors until this capture is moved or destroyed. */
    [[nodiscard]] std::span<const CapturedSpawnActor> actors() const noexcept;
    /** @brief Borrow exact sorted canonical skill records until this capture is moved or destroyed. */
    [[nodiscard]] std::span<const definitions::Definition> skills() const noexcept;

private:
    friend class RPGSpawnFactory;
    [[nodiscard]] static Result<RPGSpawnCapture> captureWithFuture(
        ecs::Table& table, PersistentId saveEpoch, std::span<const UnitRecipe> futureUnits,
        std::shared_ptr<const rpg::SkillConditionBindings> futureConditions, std::span<const std::string> eventSkills,
        std::span<const std::string> eventEffects);
    struct Impl;
    explicit RPGSpawnCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rpg_spawn
