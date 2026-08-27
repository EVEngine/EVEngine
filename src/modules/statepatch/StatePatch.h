#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/SquirrelOwnership.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eve::statepatch {

/** @brief Handle domain for module-owned state-patch stores. */
struct StateStoreHandleTag {};
/** @brief Generation- and module-epoch-qualified state store reference. */
using StateStoreHandleRef = eve::script::RuntimeHandleRef<StateStoreHandleTag>;
/** @brief Handle domain for store-owned patch batches. */
struct PatchBatchHandleTag {};
/** @brief Generation- and store-epoch-qualified patch batch reference. */
using PatchBatchHandleRef = eve::script::RuntimeHandleRef<PatchBatchHandleTag>;

/**
 * @brief A patch batch reference qualified by both its owning Store and batch slot.
 *
 * The nested store reference is part of the identity: resolving a batch after
 * its Store has been released, unloaded, or restored is a structured stale
 * failure rather than a dangling raw-pointer access.
 */
struct StateBatchHandleRef {
    StateStoreHandleRef store;
    PatchBatchHandleRef  batch;
    std::uint64_t         ownerEpoch = 0;

    /** @brief Returns the batch slot/generation projection. */
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept { return batch.packed(); }
    /** @brief Returns whether both nested references are syntactically valid. */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return store.isValid() && batch.isValid() && ownerEpoch != 0;
    }
};

class StoreTransactionParticipant;

/** @brief One validation error produced while applying a patch batch. */
struct PatchError {
    int         operationIndex = -1;
    std::string subject;
    std::string key;
    std::string code;
    std::string message;
};

/** @brief Summary of the most recent batch commit attempt. */
struct PatchResult {
    bool                    success        = false;
    int                     changedCount   = 0;
    uint64_t                revisionBefore = 0;
    uint64_t                revisionAfter  = 0;
    std::vector<PatchError> errors;
};

/** @brief Immutable description of one committed value change. */
struct ChangeEvent {
    uint64_t    sequence = 0;
    uint64_t    revision = 0;
    std::string subject;
    std::string key;
    std::string oldJson;
    std::string newJson;
    bool        removed = false;
};

/** @brief Ordered collection of set and remove operations committed atomically. */
class PatchBatch {
public:
    /** @brief Appends an unconditional set operation. */
    bool set(const std::string& subject, const std::string& key, const std::string& jsonValue);
    /** @brief Appends a set operation guarded by an expected current JSON value. */
    bool setExpected(const std::string& subject, const std::string& key, const std::string& jsonValue,
                     const std::string& expectedJson);
    /** @brief Appends an unconditional remove operation. */
    bool remove(const std::string& subject, const std::string& key);
    /** @brief Appends a remove operation guarded by an expected current JSON value. */
    bool removeExpected(const std::string& subject, const std::string& key, const std::string& expectedJson);
    /** @brief Removes every operation and resets the latest result. */
    void clear();
    /** @brief Returns the number of queued operations. */
    int size() const;
    /** @brief Returns the result of the most recent commit attempt. */
    const PatchResult& result() const;

private:
    friend class Store;
    struct Operation {
        bool                       remove = false;
        std::string                subject;
        std::string                key;
        std::string                value;
        std::optional<std::string> expected;
        std::string                inputError;
    };
    std::vector<Operation> operations_;
    PatchResult            result_;
};

/** @brief Deterministic subject-and-key JSON value store with atomic patching. */
class Store {
public:
    /** @brief Creates an empty store with an optional persistent identity. */
    explicit Store(eve::PersistentId instanceId = {});
    /**
     * @brief Allocates a store-owned empty patch batch and returns its handle.
     * @return A generation-qualified batch reference local to this Store.
     * @remarks The reference becomes stale after release, restore, Store
     *          destruction, or module unload/reload. Resolve is borrowed.
     */
    [[nodiscard]] eve::Result<PatchBatchHandleRef> newBatch();
    /** @brief Resolves a live batch as a non-owning observation. */
    [[nodiscard]] eve::script::Borrowed<PatchBatch> resolveBatch(
        PatchBatchHandleRef reference) noexcept;
    /** @brief Releases a store-owned patch batch. */
    [[nodiscard]] eve::Result<void> releaseBatch(PatchBatchHandleRef reference);
    /** @brief Reports whether a batch reference is stale for this Store. */
    [[nodiscard]] bool isBatchStale(PatchBatchHandleRef reference) const noexcept;
    /** @brief Validates and atomically commits a batch. */
    bool commit(PatchBatch* batch);
    /** @brief Returns whether a subject and key currently exist. */
    bool has(const std::string& subject, const std::string& key) const;
    /** @brief Returns canonical JSON for a value, or an empty string when absent. */
    std::string get(const std::string& subject, const std::string& key) const;
    /** @brief Returns the revision at which a value last changed, or zero when absent. */
    uint64_t valueRevision(const std::string& subject, const std::string& key) const;
    /** @brief Returns the current global store revision. */
    uint64_t revision() const;
    /** @brief Queries subjects in lexical order. */
    int querySubjects();
    /** @brief Queries keys for a subject in lexical order. */
    int queryKeys(const std::string& subject);
    /** @brief Returns an item from the latest subject or key query. */
    std::string queryAt(int index) const;
    /** @brief Queries dirty subject-key pairs in lexical order. */
    int queryDirty();
    /** @brief Returns the subject for a dirty query item. */
    std::string dirtySubjectAt(int index) const;
    /** @brief Returns the key for a dirty query item. */
    std::string dirtyKeyAt(int index) const;
    /** @brief Clears all dirty-key markers without changing values. */
    void clearDirty();
    /** @brief Returns the number of retained change events. */
    int eventCount() const;
    /** @brief Returns a retained change event, or nullptr. */
    const ChangeEvent* eventAt(int index) const;
    /** @brief Removes retained change events without resetting sequence allocation. */
    void clearEvents();
    /** @brief Exports all persistent store state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /**
     * @brief Transactionally restores a snapshot produced by snapshotJson().
     * @param json Snapshot JSON to parse and validate.
     * @return Success, or a parse/validation diagnostic; on failure the Store
     *         remains unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

    /** @brief Captures the state-patch payload in the common snapshot envelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Restores a verified or migrated state-patch envelope atomically.
     * @param snapshot Source envelope with schema `statepatch:store`.
     * @param hashProvider Explicit content-digest provider.
     * @return Success, or a failure leaving all values and metadata unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common state-patch snapshot envelope. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common state-patch envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(
        std::string_view json, const eve::SnapshotHashProvider& hashProvider);

private:
    friend class StoreTransactionParticipant;

    struct Value {
        std::string json;
        uint64_t    revision = 0;
    };
    using Values = std::map<std::string, std::map<std::string, Value>>;

    Values                                           values_;
    uint64_t                                         revision_     = 0;
    uint64_t                                         nextSequence_ = 1;
    eve::PersistentId                                instanceId_;
    eve::SimulationTick                              tick_;
    std::set<std::pair<std::string, std::string>>    dirty_;
    std::vector<ChangeEvent>                         events_;
    std::vector<std::string>                         query_;
    std::vector<std::pair<std::string, std::string>> dirtyQuery_;
    eve::script::RuntimeObjectRegistry<PatchBatch, PatchBatchHandleTag> batches_;
    void copyTransactionStateFrom(const Store& source);
    void swapTransactionState(Store& other) noexcept;
    bool transactionStateEquals(const Store& other) const;
};

/**
 * @brief Transaction participant that atomically applies one StatePatch batch.
 *
 * The participant owns neither `store` nor `batch`; both are borrowed and
 * must remain alive and unchanged by other writers until the coordinator
 * finishes. `prepare` validates against a private complete-state candidate,
 * `commit` swaps that candidate into the store, `rollback` discards it, and
 * `compensate` swaps the pre-commit state back after a later participant has
 * failed. Compensation is not a rollback: it is an explicit undo of an effect
 * that was already made observable.
 *
 * This class is synchronous and not thread-safe. It must be used on the same
 * thread as its Store, and must not be retained for a later frame or task.
 */
class StoreTransactionParticipant final : public transaction::ITransactionParticipant {
public:
    /**
     * @brief Bind one patch batch to a Store transaction.
     * @param store Borrowed authoritative Store to mutate on commit.
     * @param batch Borrowed batch whose operations are staged and validated.
     */
    StoreTransactionParticipant(Store& store, PatchBatch& batch) : store_(store), batch_(batch) {}
    ~StoreTransactionParticipant() override = default;

    /** @brief Stable participant name used in transaction diagnostics. */
    std::string_view name() const noexcept override { return "statepatch.store"; }

    /** @brief Validate and privately stage the batch. */
    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext& context) override;
    /** @brief Publish the staged batch as one Store state swap. */
    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext& context) override;
    /** @brief Discard an unpublished candidate state. */
    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext& context) override;
    /** @brief Restore the state captured before this participant committed. */
    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext& context) override;

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack, Compensated, Failed };

    [[nodiscard]] eve::Result<void> lifecycleFailure(std::string_view operation) const;
    [[nodiscard]] eve::Result<void> contextFailure(const transaction::TransactionContext& context) const;
    bool contextMatches(const transaction::TransactionContext& context) const noexcept;

    Store&                                      store_;
    PatchBatch&                                 batch_;
    std::unique_ptr<Store>                     before_;
    std::unique_ptr<Store>                     prepared_;
    std::unique_ptr<Store>                     expectedAfter_;
    std::string                                 transactionId_;
    std::string                                 correlationId_;
    std::string                                 causationId_;
    Phase                                       phase_ = Phase::Idle;
};

/** @brief Script module factory for generic state patch stores. */
class StatePatch : public Module {
public:
    Module_REG(StatePatch);
    StatePatch()           = default;
    ~StatePatch() override = default;

    /**
     * @brief Allocates a state store and returns its ownership reference.
     * @return A generation-qualified reference; the current StatePatch module owns the store.
     * @remarks The reference becomes stale after release, module unload, or reload.
     */
    [[nodiscard]] static eve::Result<StateStoreHandleRef> newStore();
    /** @brief Resolves a live store as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<Store> resolve(
        StateStoreHandleRef reference) noexcept;
    /** @brief Releases a module-owned state store. */
    [[nodiscard]] static eve::Result<void> release(StateStoreHandleRef reference);
    /** @brief Reports whether a store reference is stale for the current module. */
    [[nodiscard]] static bool isStale(StateStoreHandleRef reference) noexcept;

    /** @brief Resolves a store-qualified batch as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<PatchBatch> resolveBatch(
        StateBatchHandleRef reference) noexcept;
    /** @brief Releases a store-qualified patch batch. */
    [[nodiscard]] static eve::Result<void> releaseBatch(StateBatchHandleRef reference);
    /** @brief Reports whether a store-qualified batch reference is stale. */
    [[nodiscard]] static bool isBatchStale(StateBatchHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<Store, StateStoreHandleTag> stores_;
};

}  // namespace eve::statepatch
