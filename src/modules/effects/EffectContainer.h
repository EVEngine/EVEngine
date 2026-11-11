#pragma once
#include "common/Export.h"


#include "common/Time.h"
#include "common/UuidV7.h"

/**
 * @file EffectContainer.h
 * @brief effects module-interface: owning effect storage and lifecycle-only executor.
 * @remarks Allocation-free ownership moves preserve instance/event borrows in the destination.
 * Retained event and effects.container v1 data faces are unchanged; no new capability, ECS, script or tool face.
 */

#include "effects/EffectTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::effects {

class EffectExecutor;

/**
 * @brief Owns active effect instances and their deterministic lifecycle events.
 *
 * The container is the sole owner of instance state.  Definitions are copied
 * during apply and are not retained as borrowed pointers.  Events contain
 * owning value fields, so event consumers do not retain pointers into the
 * container.  All methods are simulation-thread confined unless the caller
 * provides external synchronization; callbacks are not invoked by this type.
 */
class EVENGINE_API_FOUNDATION EffectContainer {
public:
    /**
     * @brief Construct an empty effect container.
     */
    EffectContainer() = default;

    /**
     * @brief Construct a container whose newly-created instances use UUID identities.
     * @param entropy Entropy source used for each new effect identity.
     * @param clock Optional clock used for UUIDv7 timestamps.
     * @remarks Passing an entropy source opts into the canonical API. The legacy
     *          default constructor keeps its string projection for compatibility.
     */
    explicit EffectContainer(eve::UuidEntropySource entropy, eve::UuidClock clock = {});

    /**
     * @brief Deep-copy a container, including instances, events and counters.
     * @param other Container to copy; no instance storage is shared.
     */
    EffectContainer(const EffectContainer& other);

    /**
     * @brief Deep-copy assign a container, including instances, events and counters.
     * @param other Container to copy; no instance storage is shared.
     * @return This container.
     */
    EffectContainer& operator=(const EffectContainer& other);

    /** @brief Transfer sole instance/event ownership without allocation.
     * @remarks Borrows follow storage into the destination. Only destruction or assignment is supported on the source.
     */
    EffectContainer(EffectContainer&& other) noexcept = default;
    /** @brief Replace sole instance/event ownership without allocation.
     * @remarks Existing destination borrows expire; source borrows follow storage into the destination.
     * Only destruction or assignment is supported on the source.
     */
    EffectContainer& operator=(EffectContainer&& other) noexcept = default;

    /**
     * @brief Create an independent deep copy suitable for ECS staging.
     * @return A container whose instances, events and counters are independent.
     */
    [[nodiscard]] EffectContainer clone() const;

    /**
     * @brief Capture an independent lifecycle snapshot for staging/restore.
     * @return A deep copy; no instance ownership is shared.
     */
    [[nodiscard]] EffectContainer snapshot() const;

    /**
     * @brief Restore a previously captured snapshot as one observable commit.
     * @param snapshot Snapshot value; it is copied and never retained.
     * @return Applied on success, or a checked failure without partial state.
     * @remarks Every pre-existing handle becomes stale after a successful restore.
     */
    [[nodiscard]] eve::Result<void> restore(const EffectContainer& snapshot);

    /** @brief Encode complete lifecycle facts, events, counters and scheduler cursor as effects.container v1.
     * @return Owning portable value, or invalid facts / Unsupported for an injected UUID generator whose
     * implementation and external state have no persistent identity protocol.
     * @remarks Native handle generations and callbacks are not serialized. Initial v1 has no migration.
     * @thread Read-only simulation-owner boundary; no callbacks or services invoked.
     * @cost Copies every instance, payload and retained event once per explicit save.
     * @throws std::bad_alloc Source remains unchanged.
     */
    [[nodiscard]] eve::Result<eve::Value> encodeState() const;
    /** @brief Admit strict effects.container v1 without applying effects, replaying events or creating ECS.
     * @return Detached lifecycle container, or malformed/unknown fields, enums, identities and counter errors.
     * @remarks Owns all values independently of input. Publish through restore to invalidate old native handles.
     * @thread Pure CPU admission; no callbacks, providers or simulation advancement.
     * @cost Copies and validates all instances and events once per explicit load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static eve::Result<EffectContainer> prepareState(const eve::Value& value);

    /**
     * @brief Applies a definition and returns the stable instance identifier.
     * @param definition Definition copied for this operation; it is not retained.
     * @param subject Opaque subject identity owned by the caller/domain.
     * @param source Opaque source identity owned by the caller/domain.
     * @return The active instance id, or a structured rejection.
     * @remarks No RPG settlement or attribute mutation is performed.
     */
    [[nodiscard]] eve::Result<std::string> apply(const EffectDefinition& definition, const std::string& subject,
                                                 const std::string& source = {});

    /**
     * @brief Applies a definition and returns its UUID-backed effect identity.
     * @param definition Definition copied for this operation; it is not retained.
     * @param subject Opaque subject identity owned by the caller/domain.
     * @param source Opaque source identity owned by the caller/domain.
     * @return The canonical effect identity, or Unsupported when this container
     *         has no injected entropy source.
     * @remarks This path never creates an incrementing string identity. The
     *          returned UUID's `format()` is stored as the legacy projection.
     */
    [[nodiscard]] eve::Result<eve::EffectId> applyCanonical(const EffectDefinition& definition,
                                                            const std::string& subject, const std::string& source = {});

    /**
     * @brief Return a generation-qualified handle for a live instance.
     * @param id Local canonical instance id.
     * @return A handle valid until removal, clear, restore or destruction.
     */
    [[nodiscard]] eve::Result<EffectHandle> handleFor(const std::string& id) const;

    /**
     * @brief Resolve a generation-qualified handle for this call.
     * @param handle Handle previously returned by this container.
     * @return A borrowed instance, or StaleHandle/NotFound.
     */
    [[nodiscard]] eve::Result<const EffectInstance*> resolve(EffectHandle handle) const;

    /** @brief Return the current container generation used by effect handles. */
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    /**
     * @brief Applies scalar effect fields using the canonical Result contract.
     * @return The active instance id, or a structured rejection.
     */
    [[nodiscard]] eve::Result<std::string> apply(const std::string& subject, const std::string& type,
                                                 const std::string& source, int priority, double duration,
                                                 const std::string& stackKey, StackPolicy policy);

    /**
     * @brief Removes one active instance and emits a Removed event.
     * @return Success when removed, or NotFound when the id is not active.
     */
    [[nodiscard]] eve::Result<void> remove(const std::string& id, const std::string& reason = "removed");

    /** @brief Removes one active instance by UUID-backed identity. */
    [[nodiscard]] eve::Result<void> remove(eve::EffectId id, const std::string& reason = "removed");

    /**
     * @brief Atomically remove matching dispellable effects in deterministic priority order.
     * @param subject Subject whose effects may be removed.
     * @param categoryTag Required category tag, for example `dispel:magic`.
     * @param strength Removes effects whose priority is no greater than this strength.
     * @param maxCount Maximum removals; zero is an explicit no-op.
     * @param reason Owning lifecycle-event reason copied into every removal event.
     * @return Removed instance ids in descending priority and creation order.
     * @remarks Effects tagged `effect:undispellable` are never removed. The operation
     *          stages through the existing snapshot/remove/restore path, so failure
     *          leaves this container unchanged and invalidates handles only on commit.
     * @thread Call on the container's owning simulation thread.
     * @reentrancy No callbacks are invoked.
     * @cost Linear scan and sort plus one deep container snapshot and restore.
     */
    [[nodiscard]] eve::Result<std::vector<std::string>> dispel(const std::string& subject,
                                                               const std::string& categoryTag, int strength,
                                                               std::size_t maxCount,
                                                               const std::string& reason = "dispelled");

    /**
     * @brief Advances finite instances through the lifecycle executor.
     * @param dtSeconds Positive simulation delta; zero is a successful no-op.
     * @return Number of expired instances and the applied delta.
     */
    [[nodiscard]] eve::Result<EffectUpdateSummary> update(double dtSeconds);

    /**
     * @brief Advances lifecycle state using one scheduler-owned simulation step.
     * @param step Injected fixed-step tick and duration; no wall clock is read.
     * @return A checked lifecycle summary. Reusing or rewinding a tick is rejected
     *         and leaves the container unchanged.
     * @remarks The container is simulation-thread confined. The duration is
     *          converted to the legacy double representation only at this
     *          effect-domain boundary; persisted state is identified by tick.
     */
    [[nodiscard]] eve::Result<EffectUpdateSummary> advance(const eve::SimulationStep& step);

    /** @brief Removes all active instances and retained events, resetting id sequences. */
    void clear();

    /**
     * @brief Returns an active instance by stable id, or nullptr when absent.
     * @ownership Borrowed from this container; callers must not delete or retain it.
     * @nullable Null when the id is absent.
     * @lifetime Until this instance is removed or the container is cleared/destroyed.
     * @thread Simulation-thread confined unless the caller supplies synchronization.
     * @reentrancy No callbacks are made.
     */
    [[nodiscard]] EffectInstance* find(const std::string& id);
    /** @copydoc EffectContainer::find(const std::string&) */
    /** @brief Finds . */
    [[nodiscard]] const EffectInstance* find(const std::string& id) const;
    /**
     * @brief Returns an active instance by UUID-backed identity, or nullptr.
     * @ownership Borrowed from this container; callers must not delete or retain it.
     * @nullable Null when the identity is absent.
     * @lifetime Until this instance is removed or the container is cleared/destroyed.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    [[nodiscard]] EffectInstance* find(eve::EffectId id);
    /** @copydoc EffectContainer::find(eve::EffectId) */
    /** @brief Finds . */
    [[nodiscard]] const EffectInstance* find(eve::EffectId id) const;
    /** @brief Returns active instance count in deterministic creation order. */
    int effectCount() const;
    /**
     * @brief Returns an active instance by deterministic index, or nullptr.
     * @ownership Borrowed from this container; callers must not delete or retain it.
     * @nullable Null when the index is outside the current range.
     * @lifetime Until the addressed instance is removed or the container is cleared/destroyed.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    EffectInstance* effectAt(int index);
    /** @copydoc EffectContainer::effectAt(int) */
    /** @brief Effect at. */
    const EffectInstance* effectAt(int index) const;
    /** @brief Returns active instance count for a subject. */
    int subjectCount(const std::string& subject) const;
    /**
     * @brief Returns a subject instance by deterministic index, or nullptr.
     * @ownership Borrowed from this container; callers must not delete or retain it.
     * @nullable Null when the subject or index is absent.
     * @lifetime Until the addressed instance is removed or the container is cleared/destroyed.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    EffectInstance* subjectAt(const std::string& subject, int index);
    /** @copydoc EffectContainer::subjectAt(const std::string&, int) */
    /** @brief Subject at. */
    const EffectInstance* subjectAt(const std::string& subject, int index) const;
    /** @brief Returns active subject instances containing a tag. */
    int taggedCount(const std::string& subject, const std::string& tag) const;
    /**
     * @brief Returns a tagged subject instance by deterministic index, or nullptr.
     * @ownership Borrowed from this container; callers must not delete or retain it.
     * @nullable Null when no matching tagged instance exists at the index.
     * @lifetime Until the addressed instance is removed or the container is cleared/destroyed.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    EffectInstance* taggedAt(const std::string& subject, const std::string& tag, int index);
    /** @copydoc EffectContainer::taggedAt(const std::string&, const std::string&, int) */
    /** @brief Tagged at. */
    const EffectInstance* taggedAt(const std::string& subject, const std::string& tag, int index) const;

    /** @brief Returns retained event count. */
    int eventCount() const;
    /**
     * @brief Returns an event by deterministic sequence index, or nullptr.
     * @ownership Borrowed from this container's retained event deque.
     * @nullable Null when the index is outside the retained range.
     * @lifetime Until `clearEvents`, `clear`, restore, successful checked time update, replacement, or destruction.
     * Appending events preserves borrows. An owning move transfers their lifetime to the destination storage.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    EffectEvent* eventAt(int index);
    /** @copydoc EffectContainer::eventAt(int) */
    /** @brief Event at. */
    const EffectEvent* eventAt(int index) const;
    /** @brief Clears retained events without resetting their sequence counter. */
    void clearEvents();

    /** @brief Last scheduler tick consumed by the checked time API. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }
    /** @brief Restore the scheduler cursor after an owning world checkpoint rollback. @param tick Completed tick. */
    void restoreSchedulerTick(eve::SimulationTick tick) noexcept {
        lastTick_    = tick;
        hasLastTick_ = tick.value() != 0;
    }

private:
    using Store = std::vector<std::unique_ptr<EffectInstance>>;

    [[nodiscard]] eve::Result<void> validateState() const;

    friend class EffectExecutor;

    Store::iterator       findIterator(const std::string& id);
    Store::const_iterator findIterator(const std::string& id) const;
    Store::iterator       findIterator(eve::EffectId id);
    Store::const_iterator findIterator(eve::EffectId id) const;
    void                  emit(EffectEventKind kind, const EffectInstance& effect, const std::string& reason = {});
    std::string           effectiveKey(const EffectDefinition& definition) const;
    eve::Result<EffectUpdateSummary> advanceInstances(double dtSeconds, eve::SimulationTick tick);

    std::uint64_t                       nextId_       = 1;
    std::uint64_t                       nextSequence_ = 1;
    std::optional<eve::UuidV7Generator> effectIdGenerator_;
    std::uint64_t                       generation_  = 1;
    eve::SimulationTick                 lastTick_    = eve::SimulationTick::zero();
    bool                                hasLastTick_ = false;
    Store                               effects_;
    // Keep deque element addresses stable while transferring the queue itself without allocation.
    std::unique_ptr<std::deque<EffectEvent>> events_;
};

/**
 * @brief Executes lifecycle time progression for an EffectContainer.
 *
 * This executor only decrements durations and emits expiry events.  It does
 * not interpret magnitude, payload, tags, or perform any domain settlement.
 */
class EVENGINE_API_FOUNDATION EffectExecutor {
public:
    /**
     * @brief Advances one container by a legacy seconds delta.
     * @param container Container whose instances are advanced; it remains owner.
     * @param dtSeconds Positive simulation delta, or zero for a no-op.
     * @return Lifecycle update summary or an invalid-argument diagnostic.
     * @remarks This compatibility overload synthesizes the next local tick;
     *          scheduler-owned callers must use the SimulationStep overload.
     */
    [[nodiscard]] eve::Result<EffectUpdateSummary> advance(EffectContainer& container, double dtSeconds) const;

    /** @brief Advances using the scheduler-owned deterministic step contract. */
    [[nodiscard]] eve::Result<EffectUpdateSummary> advance(EffectContainer&           container,
                                                           const eve::SimulationStep& step) const;
};

}  // namespace eve::effects
