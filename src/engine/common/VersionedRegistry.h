#pragma once

/**
 * @file VersionedRegistry.h
 * @brief Generation-qualified storage for replaceable, non-ECS registry data.
 *
 * This registry deliberately separates three notions that are often confused:
 * a value's domain/schema version (owned by the value), its runtime
 * replacement generation (owned here), and the stream-local sequence of
 * mutation events (owned here).  A removed key remains as a tombstone so a
 * handle from an older incarnation can never become valid again by accident.
 */

#include "common/BorrowedRef.h"
#include "common/Subscription.h"
#include "common/EventSequence.h"
#include "common/Generation.h"
#include "common/SchemaVersion.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace eve {

/** @brief Canonical mutation kind emitted by a VersionedRegistry. */
enum class RegistryOperation : std::uint8_t {
    Insert,
    Replace,
    Remove,
};

/**
 * @brief Stable lowercase name for a registry mutation kind.
 * @return Borrowed, non-null, null-terminated static text.
 * @ownership Borrowed; the returned text is not allocated.
 * @nullable No; unknown enum values return `"unknown"`.
 * @lifetime Static for the process lifetime.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not access or mutate a registry.
 */
inline const char* registryOperationName(RegistryOperation operation) noexcept {
    switch (operation) {
    case RegistryOperation::Insert:
        return "insert";
    case RegistryOperation::Replace:
        return "replace";
    case RegistryOperation::Remove:
        return "remove";
    }
    return "unknown";
}

/** @brief Writes the stable registry-operation protocol name. */
inline std::ostream& operator<<(std::ostream& stream, RegistryOperation operation) {
    return stream << registryOperationName(operation);
}

/**
 * @brief A registry whose live values are qualified by a monotonically
 *        increasing runtime generation.
 *
 * `Value` is the live domain value. `EventData` is an optional, domain-owned
 * immutable projection payload for event history; it is not interpreted by
 * this class.  The registry owns all live values, tombstones and canonical
 * mutation events.  Consumers must not maintain a second mutable map.
 *
 * The class is owner-thread affine, matching `eve::Observer`.  It is not an
 * ECS container and does not perform structural mutation deferral.
 *
 * Mutations have the strong state guarantee: all fallible value, event and
 * generation-projection work is prepared in a detached State and committed
 * with a nothrow swap. Observer dispatch happens after commit. If dispatch
 * throws, the mutation remains applied and the Result is successful with an
 * Applied status and a CallbackFailure warning; the committed state is never
 * rolled back.
 *
 * @tparam Key Stable registry key. It must be copyable and orderable through
 *         `Compare`.
 * @tparam Value Stored domain value. It must be copyable when State snapshots
 *         are copied.
 * @tparam EventData Domain event projection data; defaults to no extra data.
 * @tparam Compare Strict weak ordering for keys; its State must be
 *         nothrow-swappable so the commit boundary cannot throw.
 */
template <class Key, class Value, class EventData = std::monostate,
          class Compare = std::less<Key>>
class VersionedRegistry {
    static_assert(std::is_copy_constructible_v<Key>, "VersionedRegistry keys must be copyable");
    static_assert(std::is_copy_constructible_v<Value>, "VersionedRegistry values must be copyable");
    static_assert(std::is_copy_constructible_v<EventData>, "VersionedRegistry event data must be copyable");

public:
    /** @brief Generation-qualified identity of one live incarnation or tombstone. */
    struct Handle {
        Key        key;
        Generation generation;

        /** @brief Returns false for the zero-generation sentinel. */
        [[nodiscard]] bool isValid() const noexcept { return !generation.isZero(); }
    };

    /** @brief One canonical mutation event retained by the registry. */
    struct Event {
        EventSequence   sequence;
        RegistryOperation operation = RegistryOperation::Insert;
        Key             key;
        Generation      generation;
        bool            tombstone = false;
        std::string     label;
        EventData       data{};

        /** @brief Whether this event advanced a key to a removed tombstone. */
        [[nodiscard]] bool isTombstone() const noexcept { return tombstone; }
    };

    /** @brief One stored key slot; absent value means the slot is a tombstone. */
    struct Entry {
        Generation        generation;
        std::optional<Value> value;
    };

    /**
     * @brief Owning state image used for transactional snapshot/restore.
     *
     * Entries include tombstones. Restoring this image never emits individual
     * subscription callbacks; callers re-query after a successful restore.
     */
    struct State {
        std::map<Key, Entry, Compare> entries;
        std::vector<Event>            events;
        EventSequence                 nextEventSequence{1};
    };

    static_assert(std::is_nothrow_swappable_v<State>,
                  "VersionedRegistry state must be nothrow-swappable for atomic commit");

    /** @brief Callback used to keep a legacy value field as a read-only projection of generation. */
    using GenerationProjector = std::function<void(Value&, Generation)>;
    using ChangeCallback = std::function<void(const Event&)>;

    /**
     * @brief Constructs an empty registry.
     * @param generationProjector Optional compatibility projection invoked
     *        before a value enters storage and after restore. It must not
     *        change domain identity or schema version.
     */
    explicit VersionedRegistry(GenerationProjector generationProjector = {})
        : generationProjector_(std::move(generationProjector)), observers_() {}

    VersionedRegistry(const VersionedRegistry&) = delete;
    VersionedRegistry& operator=(const VersionedRegistry&) = delete;
    VersionedRegistry(VersionedRegistry&&) = delete;
    VersionedRegistry& operator=(VersionedRegistry&&) = delete;
    ~VersionedRegistry() = default;

    /**
     * @brief Inserts a new live value, or revives a tombstoned key.
     * @param key Stable key to insert.
     * @param value Owning value to store.
     * @param data Domain data retained with the mutation event.
     * @param label Optional domain event label; the operation remains Insert.
     * @return A handle for the new incarnation, or Conflict/Failed.
     */
    [[nodiscard]] Result<Handle> insert(Key key, Value value, EventData data = {},
                                        std::string label = {}) {
        try {
            return mutate(RegistryOperation::Insert, std::move(key),
                          std::optional<Value>(std::move(value)), std::move(data),
                          std::move(label));
        } catch (const std::exception&) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        } catch (...) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        }
    }

    /**
     * @brief Replaces an existing live value and invalidates its old handle.
     * @param key Stable registry key to replace.
     * @param value Owning replacement value.
     * @param data Domain data retained with the mutation event.
     * @param label Optional domain event label; the operation remains Replace.
     * @return A handle for the replacement incarnation, or NotFound/Failed.
     */
    [[nodiscard]] Result<Handle> replace(Key key, Value value, EventData data = {},
                                         std::string label = {}) {
        try {
            return mutate(RegistryOperation::Replace, std::move(key),
                          std::optional<Value>(std::move(value)), std::move(data),
                          std::move(label));
        } catch (const std::exception&) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        } catch (...) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        }
    }

    /**
     * @brief Removes a live value while retaining its generation tombstone.
     * @param key Key to remove.
     * @param data Domain data describing the removed value.
     * @param label Optional domain event label; the operation remains Remove.
     * @return A tombstone handle, or NotFound/Failed.
     */
    [[nodiscard]] Result<Handle> remove(Key key, EventData data = {}, std::string label = {}) {
        try {
            return mutate(RegistryOperation::Remove, std::move(key), std::nullopt,
                          std::move(data), std::move(label));
        } catch (const std::exception&) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        } catch (...) {
            return failure<Handle>(DiagnosticCode::Failed,
                                   "registry mutation argument preparation failed");
        }
    }

    /**
     * @brief Resolves a live key to a borrowed value.
     * @return A borrowed reference valid until the next mutation/restore, or
     *         NotFound when the key is absent or tombstoned.
     */
    [[nodiscard]] ResultRef<const Value> resolve(const Key& key) const {
        const auto it = state_.entries.find(key);
        if (it == state_.entries.end() || !it->second.value.has_value())
            return failure<std::reference_wrapper<const Value>>(DiagnosticCode::NotFound,
                                                                "registry key is not live");
        return ResultRef<const Value>::success(std::cref(*it->second.value));
    }

    /**
     * @brief Resolves a generation-qualified handle.
     * @return A borrowed reference for the exact live incarnation, or
     *         StaleHandle/NotFound.
     */
    [[nodiscard]] ResultRef<const Value> resolve(const Handle& handle) const {
        const auto it = state_.entries.find(handle.key);
        if (it == state_.entries.end())
            return failure<std::reference_wrapper<const Value>>(DiagnosticCode::NotFound,
                                                                 "registry key is not present");
        if (handle.generation.isZero() || it->second.generation != handle.generation ||
            !it->second.value.has_value())
            return failure<std::reference_wrapper<const Value>>(DiagnosticCode::StaleHandle,
                                                                 "registry handle is stale");
        return ResultRef<const Value>::success(std::cref(*it->second.value));
    }

    /** @brief Returns the current live handle for a key. */
    [[nodiscard]] Result<Handle> handle(const Key& key) const {
        const auto it = state_.entries.find(key);
        if (it == state_.entries.end() || !it->second.value.has_value())
            return failure<Handle>(DiagnosticCode::NotFound, "registry key is not live");
        return Result<Handle>::success(Handle{key, it->second.generation});
    }

    /**
     * @brief Returns the latest generation for a live key or tombstone.
     * @remarks This is deliberately not a SchemaVersion.
     */
    [[nodiscard]] Result<Generation> generationOf(const Key& key) const {
        const auto it = state_.entries.find(key);
        if (it == state_.entries.end())
            return failure<Generation>(DiagnosticCode::NotFound, "registry key is not present");
        return Result<Generation>::success(it->second.generation);
    }

    /** @brief Returns whether a key currently has a live value. */
    [[nodiscard]] bool contains(const Key& key) const noexcept {
        const auto it = state_.entries.find(key);
        return it != state_.entries.end() && it->second.value.has_value();
    }

    /** @brief Returns whether a key is retained as a removal tombstone. */
    [[nodiscard]] bool isTombstone(const Key& key) const noexcept {
        const auto it = state_.entries.find(key);
        return it != state_.entries.end() && !it->second.value.has_value();
    }

    /** @brief Returns true when a handle cannot resolve to its exact live incarnation. */
    [[nodiscard]] bool isStale(const Handle& handle) const noexcept {
        const auto it = state_.entries.find(handle.key);
        return it == state_.entries.end() || handle.generation.isZero() ||
               it->second.generation != handle.generation || !it->second.value.has_value();
    }

    /** @brief Number of live values, excluding tombstones. */
    [[nodiscard]] std::size_t size() const noexcept {
        std::size_t count = 0;
        for (const auto& [key, entry] : state_.entries)
            if (entry.value.has_value()) ++count;
        return count;
    }

    /** @brief Number of retained tombstone slots. */
    [[nodiscard]] std::size_t tombstoneCount() const noexcept {
        std::size_t count = 0;
        for (const auto& [key, entry] : state_.entries)
            if (!entry.value.has_value()) ++count;
        return count;
    }

    /**
     * @brief Returns a live value by deterministic key order.
     * @param index Zero-based index among live entries; out of range returns nullptr.
     * @return Borrowed value, valid until the next mutation/restore.
     * @remarks This is a read-only compatibility enumeration primitive; use
     *         the checked resolve operation for key/handle access.
     * @ownership Borrowed; the registry owns the returned value.
     * @nullable Yes when `index` is out of range.
     * @lifetime Valid until the next mutation or restore, and only while this registry lives.
     * @thread Owner-thread affine; no concurrent mutation is supported.
     * @reentrancy Does not invoke observers.
     */
    [[nodiscard]] const Value* at(std::size_t index) const noexcept {
        for (const auto& [key, entry] : state_.entries) {
            if (!entry.value.has_value()) continue;
            if (index-- == 0) return &*entry.value;
        }
        return nullptr;
    }

    /**
     * @brief Returns the key for a live enumeration index, or nullptr.
     * @return Borrowed pointer into registry state; nullptr when `index` is out of range.
     * @ownership Borrowed; the registry owns the key.
     * @nullable Yes.
     * @lifetime Valid until the next mutation or restore, and only while this registry lives.
     * @thread Owner-thread affine; no concurrent mutation is supported.
     * @reentrancy Does not invoke observers.
     */
    [[nodiscard]] const Key* keyAt(std::size_t index) const noexcept {
        for (const auto& [key, entry] : state_.entries) {
            if (!entry.value.has_value()) continue;
            if (index-- == 0) return &key;
        }
        return nullptr;
    }

    /**
     * @brief Returns a retained event by insertion sequence order, or nullptr.
     * @return Borrowed pointer into registry state; nullptr when `index` is out of range.
     * @ownership Borrowed; the registry owns the event.
     * @nullable Yes.
     * @lifetime Valid until the next mutation or restore, and only while this registry lives.
     * @thread Owner-thread affine; no concurrent mutation is supported.
     * @reentrancy Does not invoke observers.
     */
    [[nodiscard]] const Event* eventAt(std::size_t index) const noexcept {
        return index < state_.events.size() ? &state_.events[index] : nullptr;
    }

    /** @brief Number of retained mutation events. */
    [[nodiscard]] std::size_t eventCount() const noexcept { return state_.events.size(); }

    /** @brief Clears retained events without changing entries or sequence monotonicity. */
    void clearEvents() noexcept { state_.events.clear(); }

    /** @brief Takes an owning copy of live entries, tombstones and event history. */
    [[nodiscard]] State snapshotState() const { return state_; }

    /**
     * @brief Transactionally replaces all registry state from an owning image.
     * @param candidate State to validate and install.
     * @return Success, or ParseError/Failed without changing current state.
     * @remarks Generation projection is applied only to the candidate. If it
     *          throws, the current state remains installed and the exception
     *          is returned as a Failed diagnostic.
     */
    [[nodiscard]] Result<void> restoreState(State candidate) {
        if (candidate.nextEventSequence.isZero())
            return failure<void>(DiagnosticCode::ParseError, "registry next event sequence must be positive");

        EventSequence previous{};
        try {
            for (auto& [key, entry] : candidate.entries) {
                if (entry.generation.isZero())
                    return failure<void>(DiagnosticCode::ParseError, "registry entry generation must be positive");
                if (entry.value.has_value()) generationProject(*entry.value, entry.generation);
            }
        } catch (const std::exception&) {
            return failure<void>(DiagnosticCode::Failed, "registry generation projection failed");
        } catch (...) {
            return failure<void>(DiagnosticCode::Failed, "registry generation projection failed");
        }
        for (const auto& event : candidate.events) {
            if (event.sequence.isZero() || (!previous.isZero() && event.sequence <= previous))
                return failure<void>(DiagnosticCode::ParseError, "registry event sequence is not increasing");
            if (event.generation.isZero())
                return failure<void>(DiagnosticCode::ParseError, "registry event generation must be positive");
            if (event.operation == RegistryOperation::Remove && !event.tombstone)
                return failure<void>(DiagnosticCode::ParseError, "remove event must describe a tombstone");
            if (event.operation != RegistryOperation::Remove && event.tombstone)
                return failure<void>(DiagnosticCode::ParseError, "only remove events may describe tombstones");
            previous = event.sequence;
        }
        if (!previous.isZero() && candidate.nextEventSequence <= previous)
            return failure<void>(DiagnosticCode::ParseError, "next event sequence must exceed retained events");

        std::swap(state_, candidate);
        return Result<void>::success();
    }

    /**
     * @brief Subscribes to successful insert, replace and remove events.
     * @param callback Synchronously invoked after a successful state commit.
     * @return Move-only owner-thread-affine cancellation token.
     * @warning Callback exceptions are caught by the mutation operation and
     *          reported as an Applied Result with a CallbackFailure warning.
     */
    [[nodiscard("retain Subscription or explicitly dispose it")]] Subscription subscribe(ChangeCallback callback) {
        return observers_.subscribe(std::move(callback));
    }

private:
    template <class T>
    static Result<T> failure(DiagnosticCode code, std::string message) {
        return Result<T>::failure(Diagnostic::error(code, std::move(message)));
    }

    void generationProject(Value& value, Generation generation) const {
        if (generationProjector_) generationProjector_(value, generation);
    }

    [[nodiscard]] Result<Generation> nextGeneration(const Entry* entry) const {
        if (!entry) return Result<Generation>::success(Generation(1));
        const auto next = entry->generation.incremented();
        if (!next || next->isZero())
            return failure<Generation>(DiagnosticCode::Failed, "registry generation exhausted");
        return Result<Generation>::success(*next);
    }

    [[nodiscard]] Result<Handle> mutate(RegistryOperation operation, Key key, std::optional<Value> value,
                                        EventData data, std::string label) {
        try {
            // Every potentially-throwing operation happens on a detached
            // candidate. The live state is touched only by the nothrow swap
            // below, so Value/EventData/projector failures cannot partially
            // publish a registry mutation.
            State candidate = state_;
            auto it = candidate.entries.find(key);
            const bool live = it != candidate.entries.end() && it->second.value.has_value();
            if (operation == RegistryOperation::Insert && live)
                return failure<Handle>(DiagnosticCode::AlreadyExists, "registry key already exists");
            if (operation == RegistryOperation::Replace && !live)
                return failure<Handle>(DiagnosticCode::NotFound, "registry key is not live");
            if (operation == RegistryOperation::Remove && !live)
                return failure<Handle>(DiagnosticCode::NotFound, "registry key is not live");
            if (operation != RegistryOperation::Remove && !value.has_value())
                return failure<Handle>(DiagnosticCode::InvariantViolation, "live registry mutation has no value");

            const auto current = it == candidate.entries.end() ? nullptr : &it->second;
            auto next = nextGeneration(current);
            if (!next.ok()) return Result<Handle>::failure(next.status());
            const auto nextSequence = candidate.nextEventSequence.incremented();
            if (!nextSequence)
                return failure<Handle>(DiagnosticCode::Failed, "registry event sequence exhausted");

            if (operation != RegistryOperation::Remove) generationProject(*value, next.value());

            candidate.events.reserve(candidate.events.size() + 1);
            Event event{candidate.nextEventSequence, operation, key, next.value(),
                        operation == RegistryOperation::Remove, std::move(label), std::move(data)};

            if (operation == RegistryOperation::Insert && it == candidate.entries.end()) {
                auto [inserted, wasInserted] = candidate.entries.try_emplace(
                    std::move(key), Entry{next.value(), std::move(value)});
                (void)wasInserted;
                it = inserted;
            } else if (operation == RegistryOperation::Remove) {
                it->second.generation = next.value();
                it->second.value.reset();
            } else {
                it->second.generation = next.value();
                it->second.value = std::move(value);
            }

            candidate.events.push_back(std::move(event));
            candidate.nextEventSequence = *nextSequence;
            // Keep the notification independent of state_.events. Observer
            // dispatch is reentrant; a nested mutation is allowed to swap or
            // reallocate the canonical event vector while callbacks run.
            const Event notification = candidate.events.back();
            const Handle handle{notification.key, notification.generation};
            Status appliedStatus = Status::success(StatusCode::Applied);
            Status callbackWarningStatus(
                StatusCode::Applied,
                {Diagnostic::warning(DiagnosticCode::CallbackFailure,
                                     "registry mutation committed; observer callback failed")});
            std::swap(state_, candidate);

            // State is already committed. A user callback is outside the
            // transaction boundary: never roll back an event that another
            // callback may already have observed. Surface the exception as a
            // successful Applied result carrying a warning instead.
            try {
                observers_.notify(notification);
            } catch (const std::exception&) {
                return Result<Handle>::success(std::move(handle), std::move(callbackWarningStatus));
            } catch (...) {
                return Result<Handle>::success(std::move(handle), std::move(callbackWarningStatus));
            }
            return Result<Handle>::success(std::move(handle), std::move(appliedStatus));
        } catch (const std::exception&) {
            return failure<Handle>(DiagnosticCode::Failed, "registry mutation preparation failed");
        } catch (...) {
            return failure<Handle>(DiagnosticCode::Failed, "registry mutation preparation failed");
        }
    }

    State               state_;
    GenerationProjector generationProjector_;
    Observer<Event>     observers_;
};

}  // namespace eve
