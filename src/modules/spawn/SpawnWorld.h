#pragma once
#include "spawn/SpawnSavedState.h"

/**
 * @file SpawnWorld.h
 * @brief Spawn module-interface: simulation ownership, typed events and deferred control.
 * @remarks Capability input is ISpawnProviderSource; no ECS inheritance or upward domain includes.
 * Event output uses SpawnEvent/Subscription. Persistence capture uses SnapshotEnvelope;
 * domain restoration and script/tool adapters remain separate boundaries.
 */

#include "common/EventSequence.h"
#include "common/RuntimeHandle.h"
#include "common/SubjectRef.h"
#include "common/Subscription.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/SpawnMap.h"

#include <memory>
#include <span>

namespace eve::spawn {
class SpawnRestoreState;
struct WorldDiagnostics;
/** @brief Domain of generation-qualified spawner slots. */
struct SpawnerTag {};
/** @brief Process-local world identity, independent from saved deterministic identity. */
struct WorldIncarnationTag {};
using WorldIncarnation = eve::detail::StrongUint64<WorldIncarnationTag>;
/** @brief Owner-, epoch- and incarnation-qualified process-local instance reference. */
struct SpawnerHandle {
    RuntimeHandle<SpawnerTag> slot;
    PersistentId              owner;
    std::uint64_t             epoch = 0;
    WorldIncarnation          incarnation;
    /** @brief Compare source incarnation, including process incarnation, owner, epoch and slot generation. */
    friend bool operator==(const SpawnerHandle&, const SpawnerHandle&) = default;
};
/** @brief Detached capture-time membership; source is an observed runtime identity, never a persistent lease. */
struct CheckpointSubject {
    SubjectRef    subject;
    SpawnerHandle source;
};
/** @brief Observable scheduler phase, independent from transient blocking. */
enum class Phase { Dormant, Running, Completed, Faulted };
/** @brief Observable reason why a scheduled batch cannot publish. */
enum class BlockReason {
    None,
    BudgetFull,
    PlacementBlocked,
    ProviderUnavailable,
    FactoryFailed,
    UnexpectedRemoval,
    ArithmeticOverflow
};
/** @brief Domain lifecycle input; globally monotonic sequence within one world epoch. */
struct LifecycleInput {
    SubjectRef    subject;
    std::uint64_t sequence = 0;
    RemovalReason reason   = RemovalReason::Death;
    SpawnerHandle source{};
};
/** @brief Strong source-local operation order used to bound domain idempotency receipts. */
struct OperationOrdinalTag {};
using OperationOrdinal = eve::detail::StrongUint64<OperationOrdinalTag>;
/**
 * @brief Revocable source lifetime observation; copies retain only a weak marker, never the source.
 * @remarks Only SpawnWorld can issue a live lease. Detach, reload and world destruction revoke all copies.
 */
class SourceLease {
public:
    /** @brief Construct an invalid observation for unchecked request storage. */
    SourceLease() = default;
    /** @brief Whether the source owner still holds this incarnation's marker; no callbacks or lookup. */
    [[nodiscard]] bool isCurrent() const noexcept { return !marker_.expired(); }
    /** @brief Return the immutable source identity carried by this observation. */
    [[nodiscard]] SpawnerHandle source() const noexcept { return identity_ ? identity_->source : SpawnerHandle{}; }
    /** @brief Borrow the immutable instance id until this lease is destroyed, assigned or moved.
     * @remarks Empty for a default lease; text is not proof of liveness, so check isCurrent first. */
    [[nodiscard]] std::string_view instanceId() const noexcept {
        return identity_ ? std::string_view(identity_->id) : std::string_view{};
    }
    /** @brief Derive the canonical source/ordinal operation identity, or reject a revoked source/zero ordinal.
     * @cost Linear in source id length, amortized once per generated batch. */
    [[nodiscard]] EVENGINE_API_FOUNDATION Result<PersistentId> operationIdentity(OperationOrdinal ordinal) const;
    /** @brief Derive the canonical member identity without exposing or retaining the source owner.
     * @cost Linear in source id length, amortized once per generated member. */
    [[nodiscard]] EVENGINE_API_FOUNDATION Result<SubjectRef> unitIdentity(OperationOrdinal ordinal,
                                                                          std::uint32_t    index) const;

private:
    friend class SpawnWorld;
    struct Identity {
        SpawnerHandle source;
        std::string   id;
    };
    SourceLease(SpawnerHandle source, std::string id, std::weak_ptr<const std::byte> marker)
        : identity_(std::make_shared<const Identity>(Identity{source, std::move(id)})), marker_(std::move(marker)) {}
    [[nodiscard]] Result<PersistentId> derive(OperationOrdinal ordinal, std::optional<std::uint32_t> unit) const;
    std::shared_ptr<const Identity>    identity_;
    std::weak_ptr<const std::byte>     marker_;
};
/** @brief Reserved, deterministic unit identity and definition in one unpublished batch. */
struct UnitRequest {
    SubjectRef    subject;
    DefinitionRef definition;
};
/** @brief Owning creation intent, stable across placement retries. */
struct BatchRequest {
    PersistentId             operation;
    std::vector<UnitRequest> units;
    SpawnerHandle            source{};
    OperationOrdinal         ordinal;
    SourceLease              lifetime;
};
/** @brief Spatial plan can be blocked without treating normal contention as failure. */
struct PlacementBlocked {};
/** @brief Owning final candidate positions or normal temporary blocking. */
using PlacementOutcome = std::variant<PlacementBlocked, std::vector<Point>>;
/** @brief Bounded space provider; requests are borrowed only for this call. */
class ISpawnPlacement {
public:
    /** @brief Release the provider. */
    virtual ~ISpawnPlacement() = default;
    /** @brief Check supported constraints before attaching; failure has no side effects. */
    [[nodiscard]] virtual Result<void> validate(const PlacementSpec& spec, const Region& region) const = 0;
    /**
     * @brief Plan candidates using the owning instance random stream; never creates domain objects.
     * @cost Bounded by unit count times attempts, amortized per scheduled batch.
     * @thread Simulation thread; no reentry to this world; no input references are retained.
     */
    [[nodiscard]] virtual Result<PlacementOutcome> plan(const BatchRequest& request, const PlacementSpec& spec,
                                                        const Region& region, std::uint64_t& randomState) = 0;
};
/** @brief Final publication either commits the whole batch or leaves it hidden for spatial retry. */
enum class CommitOutcome { Committed, PlacementBlocked };
/**
 * @brief Owning prepared domain transaction; destruction cancels unpublished local resources noexcept.
 * @remarks Commit must publish all entities/projections or none; no user callbacks during publication.
 */
class IPreparedSpawn {
public:
    /** @brief Cancel unpublished local resources without business commits or throwing. */
    virtual ~IPreparedSpawn() = default;
    /** @brief Atomically publish prepared identities; failure rolls back every provisional change.
     * @cost Proportional to the prepared batch and backend publication work, paid once per batch. */
    [[nodiscard]] virtual Result<CommitOutcome> commit() = 0;
    /** @brief Whether this token confirms prior publication; not a unit/source liveness proof.
     * @remarks Allows spatial adapters to recognize receipt replay without reserving space again.
     * @thread Owner thread only; constant query, no callbacks or mutation. */
    [[nodiscard]] virtual bool isPublished() const noexcept = 0;
};
/** @brief Preparation either reserves a complete hidden batch or reports ordinary spatial contention. */
using PrepareOutcome = std::variant<PlacementBlocked, std::unique_ptr<IPreparedSpawn>>;
/** @brief Explicit ownership decision when removing a source relationship. */
enum class DetachPolicy { KeepUnits, RemoveUnits };
/** @brief Idempotent domain cleanup request; subjects and source are borrowed only during remove. */
struct RemovalRequest {
    PersistentId            operation;
    SpawnerHandle           source;
    DetachPolicy            policy = DetachPolicy::KeepUnits;
    std::vector<SubjectRef> subjects;
};
/** @brief Proof that every requested subject was removed or released (including already absent units). */
struct RemovalReceipt {
    std::size_t completed = 0;
};
/** @brief Domain-owned hidden creation service with a lifetime-scoped read-only composition identity. */
class ISpawnFactory {
public:
    /** @brief Release the provider. */
    virtual ~ISpawnFactory() = default;
    /** @brief Borrow the actual domain factory through synchronous admission decorators.
     * @ownership Non-null borrow valid while this factory owner is retained; decorators retain their delegate.
     * @remarks Leaf factories return themselves. Decorators delegate this query to their actual owned factory,
     * never a separately registered identity. No allocation, callbacks, mutation or reentry; owner thread only.
     * This identifies composition, not persistence admission, content validity or permission to publish actors.
     */
    [[nodiscard]] virtual const ISpawnFactory& domainFactory() const noexcept { return *this; }

    /**
     * @brief Prepare hidden entities and projection storage; validate final spatial ownership.
     * @cost Linear in batch size plus backend preparation, amortized per scheduled batch.
     * @remarks Revalidate the source lifetime and matching identity in prepare and commit;
     * a revoked source must never publish units. Prepared tokens own their request copies.
     * @thread Simulation thread; no borrowed input references are retained; no world reentry.
     */
    [[nodiscard]] virtual Result<PrepareOutcome> prepare(const BatchRequest&    request,
                                                         std::span<const Point> positions) = 0;
    /**
     * @brief Atomically remove units or release their source links; repeat requests are idempotent.
     * @return Receipt covering the full request; failure preserves the previous domain state.
     * @cost Linear in tracked units, paid once per explicit source detach.
     * @thread Simulation thread; no user callbacks or retained input references.
     */
    [[nodiscard]] virtual Result<RemovalReceipt> remove(const RemovalRequest& request) = 0;
};
/**
 * @brief Shared provider lifetime lease; host owns the session, world keeps only a weak observation.
 * @remarks Host must release this session before unloading code; in-flight synchronous calls pin it.
 */
struct SpawnProviders {
    std::shared_ptr<ISpawnPlacement> placement;
    std::shared_ptr<ISpawnFactory>   factory;
};
/** @brief Observed source authority and the last ordinal that cannot be prepared again after restoration.
 * @remarks The retired range includes committed or abandoned attempts; a stable pending attempt is excluded.
 */
struct SourceRestoreBinding {
    SourceLease      lifetime;
    OperationOrdinal retiredThrough;
};
/** @brief Owning synchronous projection for a domain restore coordinator; never a persistent format.
 * @ownership Membership/progress is copied; leases weakly observe this world and are revoked by detach/destruction.
 * @remarks Facts describe one idle boundary only. Do not advance/mutate the world while materializing its domain.
 */
struct SpawnRestoreBindings {
    /** @brief Bound save provenance; nil/zero for ordinary construction or a subsequent map attachment. */
    PersistentId                      saveEpoch;
    Revision                          revision;
    SimulationTick                    tick;
    std::uint64_t                     lifecycleSequence = 0;
    std::vector<SourceRestoreBinding> sources;
    std::vector<SavedSpawnMember>     membership;
    /** @brief Pin the actual bound execution factory for this synchronous coordinator projection. */
    std::shared_ptr<ISpawnFactory> factory;
};
/**
 * @brief Composition-root service providing a map-scoped owning provider session.
 * @remarks Register/revoke through cap::provide/revoke on the owner thread; no hot-path lookup.
 */
class ISpawnProviderSource {
public:
    static constexpr const char* capabilityName = "eve.spawn.provider-source.v1";
    /** @brief Release the composition service after revoking its capability registration. */
    virtual ~ISpawnProviderSource() = default;
    /** @brief Return an owning session; the host retains it until explicit provider teardown. */
    [[nodiscard]] virtual Result<std::shared_ptr<SpawnProviders>> acquire() = 0;
};
/**
 * @brief Borrowed activation facts for one advance; no references survive the call.
 * @remarks Absent players preserves manual proximity control; present empty players pauses proximity sources.
 * Events are resolved source handles, activating event-configured instances until explicitly paused.
 * Each input span is bounded by queuedCommands. Player queries share the round-robin instance work cap.
 */
struct ActivationFrame {
    std::optional<std::span<const Point>> players;
    std::span<const SpawnerHandle>        events;
};
/**
 * @brief Bind the registered provider source once at map composition time.
 * @return Owning session or NotFound/CallbackFailure; no provider pointer is retained.
 * @cost One capability lookup plus provider admission, amortized once per map attachment.
 * @thread Capability owner thread; service must remain registered during this synchronous call.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::shared_ptr<SpawnProviders>> bindSpawnProviders();
/** @brief Work caps validated before execution; larger batches fail admission. */
struct WorkLimits {
    std::uint32_t instancesPerTick  = 1024;
    std::uint32_t batchesPerTick    = 64;
    std::uint32_t unitsPerTick      = 1024;
    std::uint32_t candidatesPerTick = 16384;
    std::uint32_t queuedCommands    = 1024;
};
/** @brief Typed lifecycle observation published after the complete simulation step. */
enum class SpawnEventKind {
    Activated,
    Paused,
    BatchCommitted,
    Blocked,
    Unblocked,
    WaveCompleted,
    Completed,
    Faulted,
    Detached,
    Reconciled,
    RetryRequested
};
/** @brief Owning typed event; source identities remain diagnostic values after detach. */
struct SpawnEvent {
    EventSequence           sequence;
    SimulationTick          tick;
    SpawnerHandle           source;
    SpawnEventKind          kind   = SpawnEventKind::Activated;
    BlockReason             reason = BlockReason::None;
    PersistentId            operation;
    std::size_t             wave = 0;
    std::vector<SubjectRef> subjects;
};
/** @brief Control applied at the next advance boundary; callbacks never mutate the current step. */
enum class ControlKind { Activate, Pause, ReconcilePopulation, RetryFault };
/** @brief Owning deferred control request. */
struct ControlCommand {
    SpawnerHandle source;
    ControlKind   kind = ControlKind::Activate;
};
/** @brief Explicit change status; unchanged operations have no backend effects. */
enum class Change { Changed, Unchanged };
/** @brief Owning detach proof; the source handle is stale after success. */
struct DetachReceipt {
    /** @brief Accepted deferred controls cancelled by this explicit detach. */
    std::size_t             cancelledCommands = 0;
    std::size_t             released          = 0;
    std::vector<SpawnEvent> events;
    std::vector<Diagnostic> diagnostics;
};
/** @brief Owning read-only per-instance diagnostics; no getters advance simulation. */
struct SpawnerSnapshot {
    std::string             id;
    Phase                   phase   = Phase::Dormant;
    BlockReason             blocked = BlockReason::None;
    std::uint32_t           alive   = 0;
    std::uint64_t           total   = 0;
    std::size_t             wave    = 0;
    Duration                remaining;
    std::vector<SubjectRef> subjects;
};
/** @brief Owning batch execution observations; per-instance failures do not roll back other commits. */
struct AdvanceReport {
    std::uint32_t           visited = 0;
    std::uint32_t           batches = 0;
    std::uint32_t           units   = 0;
    std::vector<Diagnostic> diagnostics;
    std::vector<SpawnEvent> events;
};
/**
 * @brief Simulation-thread registry owning all spawn progress and source relationships.
 * @remarks Core has no ECS View. Creation happens after external domain View scopes have ended.
 * Providers and observers must not move or destroy this world while a call is in progress.
 * @thread All calls on the simulation owner thread; advance is non-reentrant; no locks are held in callbacks.
 */
class EVENGINE_API_FOUNDATION SpawnWorld {
public:
    /** @brief Create an owning world; owner must be non-nil and identifies the durable spawn namespace.
     * @remarks Each construction receives a distinct process incarnation; moves preserve it.
     * Incarnation is excluded from deterministic unit identities and persistent snapshots. */
    SpawnWorld(PersistentId owner, std::uint64_t seed, WorkLimits limits = {});
    /** @brief Release local scheduler data; does not destroy domain-owned units or submit cleanup. */
    ~SpawnWorld();
    SpawnWorld(const SpawnWorld&)            = delete;
    SpawnWorld& operator=(const SpawnWorld&) = delete;
    /** @brief Transfer the complete world; existing handles follow the transferred owner. */
    SpawnWorld(SpawnWorld&&) noexcept;
    /** @brief Transfer world ownership, abandoning local source tracking without deleting units. */
    SpawnWorld& operator=(SpawnWorld&&) noexcept;
    /**
     * @brief Attach all map instances after validating provider capabilities and work budgets.
     * @return Owning handles, or failure leaving this empty world unchanged; map data is copied. Handles/slots follow
     * stable instance-ID order.
     * @cost Linear in instances, members and geometry, amortized once per map load.
     */
    [[nodiscard]] Result<std::vector<SpawnerHandle>> attach(const CompiledMap&                     map,
                                                            const std::shared_ptr<SpawnProviders>& providers);
    /** @brief Observe a current source incarnation without retaining its lifetime; stale sources fail. */
    [[nodiscard]] Result<SourceLease> sourceLease(SpawnerHandle handle) const;
    /** @brief Resolve a saved source address against this world and issue its current process-local lease.
     * @return Current lease or StaleHandle for a different owner/epoch, retired slot or generation mismatch.
     * @ownership Lease observes this world weakly; moving the world preserves it, destruction/detach revokes it.
     * @remarks Intended for the restore coordinator; a saved address alone proves no domain relation or liveness.
     * @thread Simulation owner thread; no callbacks or mutation. Success allocates no memory.
     */
    [[nodiscard]] Result<SourceLease> resolveSavedSource(const SavedSpawnerKey& source) const;
    /** @brief Copy current source progress and durable membership for synchronous domain materialization.
     * @return Owning idle-boundary projection or PreconditionViolation for a moved, unattached or busy world.
     * @thread Simulation owner thread, no callbacks or mutation; caller must keep this boundary stable until commit.
     * @cost Copies all source bindings and tracked members, linear per domain restore transaction.
     * @throws std::bad_alloc Local projection allocation failed without world changes.
     */
    [[nodiscard]] Result<SpawnRestoreBindings> restoreBindings() const;
    /** @brief Retire this idle core and revoke every source lease without touching domain-owned actors.
     * @return Success, including repeated retirement; busy/moved cores fail before mutation.
     * @remarks Disables attach/advance/capture on this owner. Observers and CPU facts remain owned until destruction.
     * @thread Simulation owner thread, outside advance/notification; no callbacks, cleanup services or locks.
     * @cost Linear marker release per source, once per whole-session owner replacement; success allocates no memory.
     */
    [[nodiscard]] Result<void> retireSources();
    /** @brief Bind admitted scheduler facts to actual providers and rebuild every live source incarnation.
     * @return Independent owning core world, consuming admitted only on success; failure preserves it for retry.
     * @ownership Does not touch an old running world or domain entities. Providers remain weakly observed; caller
     * retains them. The coordinator must materialize domain members and map pending inputs before publishing or
     * advancing this returned core. Observers are never restored.
     * @thread Simulation owner thread; same-candidate reentry rejects. Placement validation callbacks must not
     * move/destroy admitted or mutate provider bindings; no locks are held or factory operations invoked.
     * @remarks Preserves durable source keys, RNG, timing, operation ordinals and membership; assigns a fresh
     * incarnation, source markers/leases and remaps pending requests, commands and events. This is not joint world
     * publication.
     * @cost Placement admission and marker/lease allocation linear in occupied sources per restore binding.
     * @throws std::bad_alloc Local allocation failed before candidate consumption or source publication.
     */
    [[nodiscard]] static Result<SpawnWorld> restore(SpawnRestoreState&                     admitted,
                                                    const std::shared_ptr<SpawnProviders>& providers);
    /** @brief Pause/freeze or resume; events publish at the next advance. Queue during observer dispatch. */
    [[nodiscard]] Result<void> setActive(SpawnerHandle handle, bool active);
    /**
     * @brief Explicitly release source links or remove the domain units before invalidating this source.
     * @return Owning receipt; failure keeps tracking, budget and handle valid for a retry.
     * @cost Linear in source population, amortized per explicit detach.
     * @thread Owner thread; forbidden during advance or event dispatch; callbacks run only after commit.
     */
    [[nodiscard]] Result<DetachReceipt> detach(SpawnerHandle handle, DetachPolicy policy);
    /** @brief Queue a bounded owning command, including during event dispatch; effects begin next advance. */
    [[nodiscard]] Result<void> enqueue(ControlCommand command);
    /**
     * @brief Observe typed events using the common mutation-safe Subscription lifecycle.
     * @return Owned subscription, or failure for an empty callback or registration during advance.
     * @remarks Observers may inspect, enqueue, subscribe or dispose. Direct mutations/reentry are rejected.
     * Callback exceptions become diagnostics after committed state; later observers still run.
     * @thread Owner thread; event references are borrowed only during each callback.
     */
    [[nodiscard]] Result<Subscription> subscribe(Observer<SpawnEvent>::Callback callback);
    /**
     * @brief Consume a monotonic tick, lifecycle and activation facts, then publish bounded scheduled batches.
     * @return Report with per-instance diagnostics; invalid inputs fail before any mutation.
     * @remarks Queued controls apply first, then supplied activation facts; proximity uses enter/exit hysteresis.
     * Sources beyond the round-robin visit cap retain their activation until visited.
     * @thread Simulation owner thread; input spans borrowed only for this call; no reentry.
     * @cost Bounded by configured instance, unit and candidate limits per simulation step.
     */
    [[nodiscard]] Result<AdvanceReport> advance(const SimulationStep&           step,
                                                std::span<const LifecycleInput> lifecycle  = {},
                                                ActivationFrame                 activation = {});
    /** @brief Read the last consumed tick without capturing diagnostics or allocating.
     * @return Zero before admission or on a moved-from world; otherwise the actual scheduler clock boundary.
     * @remarks Consumption precedes per-source work and is not proof that the full frame completed. After an
     * exception, a consumed tick must not be replayed: already published batches are not rolled back.
     * @thread Simulation owner thread; no callbacks, mutation or retained references.
     */
    [[nodiscard]] SimulationTick lastConsumedTick() const noexcept;
    /** @brief Read the actual shared bound for player/event activation inputs; zero after move.
     * @ownership Value only; no callbacks, allocation or mutation. @thread Simulation/content owner thread. */
    [[nodiscard]] std::uint32_t activationInputLimit() const noexcept;
    /**
     * @brief Capture independent diagnostic data for one generation-qualified instance.
     * @return Owning snapshot, or StaleHandle/PreconditionViolation; no side effects or callbacks.
     * @cost Linear in that instance's tracked units, amortized per explicit inspector refresh.
     */
    [[nodiscard]] Result<SpawnerSnapshot> inspect(SpawnerHandle handle) const;
    /** @brief Build one owning read-only projection of actual instance and shared-budget ledgers.
     * @return Complete published snapshot or PreconditionViolation for detached/moved/advancing worlds.
     * @ownership Output has no world/provider references; template revisions retain their actual immutable owners.
     * @thread Simulation owner thread, including committed observer dispatch; no callbacks, ticks, RNG or events.
     * @cost Linear in slots, tracked subjects and budgets per explicit inspector refresh; batch UI refreshes.
     * @see SpawnDiagnostics.h for the complete builder output; this is not a save or restore authority.
     */
    [[nodiscard]] Result<WorldDiagnostics> captureDiagnostics() const;
    /** @brief Capture complete scheduler data into an opaque owning checkpoint; creates no domain state.
     * @param saveEpoch Non-nil joint-save identity shared with all domain participants.
     * @param checkpointRevision Nonzero save-coordinator revision, independent of runtime identity epochs.
     * @return Frozen core checkpoint; call seal explicitly to create a SnapshotEnvelope.
     * @thread Owner thread at an idle attached-world boundary; invokes no callbacks.
     * @remarks Domain entities/providers/subscriptions are excluded. This alone is not a complete joint save.
     * @cost Copies slots, pinned revisions, tracked units and queued work, plus ordered revision deduplication;
     * amortized per checkpoint. Sealing has its own explicit cost boundary.
     */
    [[nodiscard]] Result<SpawnCheckpoint> capture(PersistentId saveEpoch, Revision checkpointRevision) const;

private:
    friend class SpawnRestoreState;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spawn
