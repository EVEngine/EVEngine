#pragma once

#include "common/Time.h"

/**
 * @file EffectContainer.h
 * @brief Owning effect storage and lifecycle-only executor.
 */

#include "effects/EffectTypes.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

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
class EffectContainer {
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

    /** @brief Move an owning container without sharing instance storage. */
    EffectContainer(EffectContainer&& other) noexcept = default;
    /** @brief Move-assign an owning container without sharing instance storage. */
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
    const EffectInstance* taggedAt(const std::string& subject, const std::string& tag, int index) const;

    /** @brief Returns retained event count. */
    int eventCount() const;
    /**
     * @brief Returns an event by deterministic sequence index, or nullptr.
     * @ownership Borrowed from this container's retained event deque.
     * @nullable Null when the index is outside the retained range.
     * @lifetime Until `clearEvents`, `clear`, restore, or destruction; do not retain it across those calls.
     * @thread Simulation-thread confined unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    EffectEvent* eventAt(int index);
    /** @copydoc EffectContainer::eventAt(int) */
    const EffectEvent* eventAt(int index) const;
    /** @brief Clears retained events without resetting their sequence counter. */
    void clearEvents();

    /** @brief Last scheduler tick consumed by the checked time API. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }

private:
    using Store = std::deque<std::unique_ptr<EffectInstance>>;

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
    std::deque<EffectEvent>             events_;
};

/**
 * @brief Executes lifecycle time progression for an EffectContainer.
 *
 * This executor only decrements durations and emits expiry events.  It does
 * not interpret magnitude, payload, tags, or perform any domain settlement.
 */
class EffectExecutor {
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
