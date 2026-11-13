#pragma once

/**
 * @file RPGSpawnFactory.h
 * @brief rpg_spawn module-interface: native actor factory, ECS projection and lifecycle translation.
 * @remarks Capability input is the injected catalogue/world; output implements ISpawnFactory.
 * ECS data is SpawnedActorState, events translate VitalsEvent handles to LifecycleInput.
 * Script/tool and snapshot adapters are separate integration boundaries.
 */

#include "common/Export.h"
#include "common/definitions/DefinitionRuntime.h"
#include "spawn/SpawnWorld.h"

namespace ecs {
class Table;
}
namespace eve::rpg {
struct VitalsEvent;
class SkillConditionBindings;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
class RPGUnitCatalogue;
class RPGSpawnCapture;
class RPGSpawnRestoreState;
class RPGSpawnJointRestore;
struct UnitRecipe;
/**
 * @brief Authoritative World2D position/content provenance with a derived origin projection.
 * @remarks Orthogonal component, not an entity superclass. Origin clears on death or KeepUnits.
 * Stable identity is owned solely by rpg::ActorIdentity and survives origin release.
 * SpawnWorld owns source membership; the factory index and origin field are derived routing/query projections.
 */
struct SpawnedActorState {
    std::optional<spawn::SpawnerHandle> origin;
    spawn::Point                        position;
    definition::DefinitionHandle        definition;
    /** @brief Owning immutable recipe revision, shared by actors from the same prepared batch. */
    std::shared_ptr<const UnitRecipe> pinnedRecipe;
};
/** @brief Owning observation read from the actual actor, not a duplicate health/position store. */
struct ActorSnapshot {
    SubjectRef                          subject;
    std::optional<spawn::SpawnerHandle> origin;
    spawn::Point                        position;
    double                              hp     = 0;
    double                              attack = 0;
    definition::DefinitionHandle        definition;
};
/**
 * @brief Real RPG actor factory and stable-handle lifecycle bridge.
 * @ownership Observes a weak domain table; content is shared with its owner. Tokens never retain actor pointers.
 * @thread Domain ECS owner thread; prepare/commit/remove are non-reentrant and must run outside Views.
 * @remarks CPU recipes and initial actor facts prepare without publishing actors. Commit copies an attempt batch first,
 * then creates and initializes synchronously,
 * rolls back the whole prefix on failure, then publishes preallocated identity indexes without callbacks.
 * @cost Identity admission scans the current RPG roster per created actor, once per explicit batch;
 * creation also copies each prepared body and admits its component storage.
 */
class EVENGINE_API_PLATFORM RPGSpawnFactory final : public spawn::ISpawnFactory {
public:
    /** @brief Bind a domain table, canonical content and optional actual immutable skill condition executors.
     * @ownership Shares actual policy owner with prototypes and created actors; missing required policy bindings
     * fail during prepare, before entities exist. No callback is invoked by factory preparation/commit.
     */
    RPGSpawnFactory(std::shared_ptr<ecs::Table> world, std::shared_ptr<RPGUnitCatalogue> catalogue,
                    std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Prepare a real factory using explicit archived future recipes instead of a live unit catalogue.
     * @return Unpublished factory or missing/consumed owner failure; no actor, lease or index is created.
     * @ownership Observes world weakly and shares the complete immutable admitted state, including its condition
     * executors. The caller may release its state owner after success; no latest-unit fallback is installed.
     * @thread Domain owner thread, outside Views; no callbacks. Actual content/code is checked at commit/capture.
     * @remarks This does not materialize saved actors, rebuild sources or publish a joint restored world.
     * @cost Factory/index allocation per restored session; shared archive retains its admitted actor candidates.
     * @throws std::bad_alloc Local allocation failed without publication.
     */
    [[nodiscard]] static Result<std::unique_ptr<RPGSpawnFactory>> prepareRestored(
        std::shared_ptr<ecs::Table> world, std::shared_ptr<const RPGSpawnRestoreState> restored);
    /** @brief Materialize all saved actors and source indexes as one transaction into this factory's empty domain.
     * @param core Idle reconstructed core; kept unchanged throughout this synchronous transaction.
     * @param admitted Same checked joint candidate whose domainOwner is installed in this factory; no external
     * pending override is accepted. Candidate remains owning/reusable on either outcome.
     * @return Success or failure retaining archive/factory/core and rolling back every created actor.
     * @ownership Requires an archive-backed pristine factory and a domain Table without RPG actors. Retained
     * origin-free actors are restored in the Table, while only source-owned actors enter the routing index.
     * @remarks Revalidates source/membership/vital relations and exact actual content/code before creation.
     * Installs retired operation floors and lifecycle sequence past core/pending. Old payloads below floors return
     * StaleHandle, never fabricated replay receipts. This does not replace an old world or restore map participants.
     * @thread Domain owner thread, outside Views and callbacks; no gameplay callbacks or events are emitted.
     * @cost Whole archive member cloning plus ECS/component/index allocation per materialization attempt.
     * @throws std::bad_alloc Local staging failed before publication; the same factory/archive may retry.
     */
    [[nodiscard]] Result<void> materializeSavedActors(const spawn::SpawnWorld&    core,
                                                      const RPGSpawnJointRestore& admitted);
    /** @brief Freeze the actual domain table and this factory's exact future recipes/condition owner.
     * @param saveEpoch Non-nil joint save identity.
     * @param references Sorted or unsorted core checkpoint references; duplicates are rejected, all must resolve.
     * @param eventSkills Canonical pending-event skill IDs, borrowed only for this call.
     * @param eventEffects Canonical pending-event effect IDs, borrowed only for this call.
     * @return Owning domain participant or failure without source mutation, entity creation or callbacks.
     * @ownership Reads the actual catalogue/condition owner already installed in this factory; no caller-built
     * recipe or substitute policy table can impersonate future execution bindings. Output retains no factory/Table.
     * @thread Domain owner thread, outside Views and factory operations; failure retains live ownership.
     * @cost Full table checkpoint capture plus canonical recipe/skill/effect closure per future reference, per save.
     * @throws std::bad_alloc Local allocation failed without changing live state.
     */
    [[nodiscard]] Result<RPGSpawnCapture> capture(PersistentId saveEpoch, std::span<const DefinitionRef> references,
                                                  std::span<const std::string> eventSkills  = {},
                                                  std::span<const std::string> eventEffects = {}) const;
    /** @brief Release derived indexes; actors remain owned by their domain table. */
    ~RPGSpawnFactory() override;
    RPGSpawnFactory(const RPGSpawnFactory&)            = delete;
    RPGSpawnFactory& operator=(const RPGSpawnFactory&) = delete;
    /**
     * @brief Pin owning unit recipes and unpublished actor facts for an atomic creation.
     * @return Hidden transaction or structured failure. Latest identical receipt replays are no-ops; older ordinals
     * return StaleHandle and never recreate units. Receipts are bounded per source and world epoch.
     * @cost Linear in units, recipe resolution and prepared actor data, amortized per scheduled batch.
     */
    [[nodiscard]] Result<spawn::PrepareOutcome> prepare(const spawn::BatchRequest&    request,
                                                        std::span<const spawn::Point> positions) override;
    /**
     * @brief Idempotently remove actors or clear origin links; stale actors count as already absent.
     * @return Complete receipt; invalid identity/source requests leave the domain untouched.
     * @cost Linear in requested units plus index lookups, once per explicit detach.
     */
    [[nodiscard]] Result<spawn::RemovalReceipt> remove(const spawn::RemovalRequest& request) override;
    /** @brief Read the actual live actor through its generation handle; failure never returns a borrowed pointer. */
    [[nodiscard]] Result<ActorSnapshot> inspect(SubjectRef subject) const;
    /** @brief Apply canonical HP damage to one currently source-owned actor by persistent identity.
     * @param subject Identity from a current spawn receipt/observation; stale sources or actors are rejected.
     * @param amount Finite nonnegative HP damage; zero and an already dead actor apply zero damage.
     * @return Actual damage, or validation/lifetime failure without mutation.
     * @ownership Resolves a generation handle synchronously under the actual Table owner; no pointer escapes.
     * @thread Domain owner thread outside Views; rejects reentry and holds no locks.
     * @reentrancy Canonical attribute calculators may run; they must not mutate/destroy actor, Table or owners.
     * @remarks Publishes real VitalsEvents to the caller's active SimulationEventScope; caller owns event handoff.
     * Canonical damage prepares events before HP mutation. This does not advance time or update spawn membership.
     * @cost Logarithmic identity lookup plus canonical resource/event preparation per explicit damage action.
     * @throws std::bad_alloc Event preparation failed before HP/event mutation.
     */
    [[nodiscard]] Result<double> applyDamage(SubjectRef subject, double amount);
    /**
     * @brief Translate already-polled domain events and stale entities into ordered Spawn lifecycle facts.
     * @param events Synchronously borrowed RPG event batch, shared by caller with other gameplay consumers.
     * @return Owning facts; death beats destruction when both appear in this batch; the queue is not stolen.
     * @cost Linear in tracked actors and input events with logarithmic index lookup, once per simulation step.
     * @thread Domain owner thread outside Views; no callbacks. Call once per event batch.
     */
    [[nodiscard]] Result<std::vector<spawn::LifecycleInput>> collectLifecycle(std::span<const rpg::VitalsEvent> events);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::rpg_spawn
