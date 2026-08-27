#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "common/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::transaction {

/**
 * @brief Immutable context shared by every participant of one transaction.
 *
 * The coordinator copies the identifiers supplied here into its receipt and
 * passes this same context to every lifecycle call. The context is borrowed
 * only for the duration of a synchronous call; participants must copy values
 * they need after that call. The coordinator and participants must run on the
 * same thread unless a participant explicitly documents another affinity.
 */
class TransactionContext {
public:
    /**
     * @brief Construct transaction correlation metadata.
     * @param transactionId Stable identifier for this transaction.
     * @param correlationId Identifier of the business chain, if any.
     * @param causationId Identifier of the command/event that caused it, if any.
     */
    TransactionContext(std::string transactionId, std::string correlationId = {}, std::string causationId = {})
        : transactionId_(std::move(transactionId)), correlationId_(std::move(correlationId)),
          causationId_(std::move(causationId)) {}

    /**
     * @brief Construct a canonical UUID-backed transaction context.
     * @param transactionId Non-nil cross-process transaction identity.
     * @param correlationId Identifier of the business chain, if any.
     * @param causationId Identifier of the command/event that caused it, if any.
     * @remarks The string accessor remains a compatibility projection of the UUID.
     */
    explicit TransactionContext(eve::TransactionId transactionId, std::string correlationId = {},
                                std::string causationId = {})
        : identity_(transactionId),
          transactionId_(transactionId.isNil() ? std::string{} : transactionId.format()),
          correlationId_(std::move(correlationId)), causationId_(std::move(causationId)) {}

    /** @brief Canonical UUID-backed identity; nil only for the legacy string facade. */
    [[nodiscard]] const eve::TransactionId& identity() const noexcept { return identity_; }
    /** @brief Stable identifier projection retained for compatibility. */
    const std::string& transactionId() const noexcept { return transactionId_; }
    /** @brief Business-chain correlation identifier, possibly empty. */
    const std::string& correlationId() const noexcept { return correlationId_; }
    /** @brief Directly causing command/event identifier, possibly empty. */
    const std::string& causationId() const noexcept { return causationId_; }

private:
    eve::TransactionId identity_;
    std::string transactionId_;
    std::string correlationId_;
    std::string causationId_;
};

/** @brief Terminal or in-flight outcome of coordinator-managed participants. */
enum class CoordinatorState {
    Committed,
    Compensated,
    CompensationFailed,
    RolledBack,
    RollbackFailed,
    PartiallyCommitted,
};

/**
 * @brief Observable summary returned after an atomic participant commit.
 *
 * A failed Result does not carry a receipt. The state values describing
 * rollback failure and partial commit are included in failure diagnostics;
 * they are also used by successful receipts for the committed state.
 */
struct TransactionReceipt {
    /** @brief Canonical transaction identity; nil for legacy string contexts. */
    eve::TransactionId identity;
    std::string      transactionId;
    std::string      correlationId;
    std::string      causationId;
    CoordinatorState state             = CoordinatorState::Committed;
    std::size_t      participantCount  = 0;
    std::size_t      preparedCount     = 0;
    std::size_t      committedCount    = 0;
    std::size_t      rolledBackCount   = 0;
    std::size_t      compensatedCount  = 0;
};

/**
 * @brief Synchronous participant contract for one coordinator transaction.
 *
 * `prepare` must validate and stage local changes without exposing them to
 * other systems. A successful `commit` makes that participant's staged state
 * observable. `rollback` releases a prepared but uncommitted stage. It must
 * not be used to undo an already committed external effect; that operation is
 * `compensate`. A participant that cannot compensate must return
 * `Unsupported` explicitly, allowing the coordinator to report a partial
 * commit instead of silently claiming atomicity.
 *
 * The coordinator does not own or retain participants. They, their backing
 * stores, and any input objects must outlive the synchronous `execute` call.
 * Implementations must not invoke unknown callbacks or scripts while holding
 * a lock, and must leave their own state unchanged when a lifecycle method
 * returns a failure Result.
 */
class ITransactionParticipant {
public:
    virtual ~ITransactionParticipant() = default;

    /**
     * @brief Stable diagnostic name for this participant.
     * @return A non-owning name valid for the duration of a coordinator call.
     */
    virtual std::string_view name() const noexcept { return "transaction-participant"; }

    /**
     * @brief Validate and stage local changes for the transaction.
     * @param context Shared borrowed transaction context.
     * @return Success when this participant is prepared, otherwise diagnostics.
     */
    [[nodiscard]] virtual Result<void> prepare(const TransactionContext& context) = 0;

    /**
     * @brief Publish this participant's staged changes.
     * @param context Shared borrowed transaction context.
     * @return Success when the participant is committed, otherwise diagnostics.
     */
    [[nodiscard]] virtual Result<void> commit(const TransactionContext& context) = 0;

    /**
     * @brief Discard a prepared but not committed stage.
     * @param context Shared borrowed transaction context.
     * @return Success when provisional state is discarded, otherwise diagnostics.
     */
    [[nodiscard]] virtual Result<void> rollback(const TransactionContext& context) = 0;

    /**
     * @brief Undo an already committed external effect by compensation.
     * @param context Shared borrowed transaction context.
     * @return Success when compensation completes, `Unsupported` when impossible,
     *         or another failure with diagnostics.
     */
    [[nodiscard]] virtual Result<void> compensate(const TransactionContext& context) = 0;
};

/**
 * @brief Coordinates participant lifecycles with deterministic ordering.
 *
 * Participants prepare in input order. On prepare failure, prepared
 * participants roll back in reverse order. On commit failure, uncommitted
 * participants roll back in reverse order and committed participants
 * compensate in reverse order. The coordinator never substitutes rollback
 * for compensation and never hides a failed compensation.
 */
class Coordinator {
public:
    /**
     * @brief Atomically execute borrowed participants for one context.
     * @param context Immutable transaction metadata shared with all participants.
     * @param participants Non-owning participant pointers, unique and non-null;
     *        order defines prepare and commit order.
     * @return A committed receipt, or a failure describing the first error and
     *         any rollback/compensation failure.
     * @thread Synchronous; caller and participants must obey their documented
     *         thread affinity and must not re-enter this coordinator call.
     */
    [[nodiscard]] Result<TransactionReceipt> execute(const TransactionContext& context,
                                                     std::span<ITransactionParticipant*> participants) const;

    /**
     * @brief Compensate already committed participants in reverse dependency order.
     * @param context Immutable metadata for the compensation operation.
     * @param participants Non-owning participants whose committed effects are to be undone.
     * @return A compensated receipt, or a failure containing every compensation
     *         diagnostic when one or more effects could not be undone.
     * @remarks Compensation is intentionally separate from rollback: rollback
     *          is only for a prepared participant that was never committed.
     */
    [[nodiscard]] Result<TransactionReceipt> compensate(const TransactionContext& context,
                                                        std::span<ITransactionParticipant*> participants) const;
};

/** @brief Lifecycle state of a generic transaction plan. */
enum class State { Open, Validated, Committed, RolledBack, Failed };

/** @brief One inert, script-interpreted operation in a transaction plan. */
struct Operation {
    std::string id;
    std::string kind;
    std::string target;
    std::string payload = "null";
    bool        valid   = false;
    bool        checked = false;
    std::string error;
    /** @brief Canonical operation identity; nil for legacy string-only plans. */
    eve::OperationId identity;
};

/** @brief Deterministically ordered transaction lifecycle event. */
struct Event {
    uint64_t    sequence = 0;
    std::string transactionId;
    std::string operationId;
    std::string type;
    std::string detail;
    /** @brief Canonical transaction identity projection, when available. */
    eve::TransactionId transactionIdentity;
    /** @brief Canonical operation identity projection, when available. */
    eve::OperationId operationIdentity;
};

/**
 * @brief Immutable-after-validation operation plan for script-coordinated work.
 *
 * The plan records intent and validation results only. It deliberately does
 * not execute operations against other engine modules.
 */
class Plan {
public:
    /** @brief Returns the stable ledger-local transaction identifier. */
    const std::string& id() const;
    /** @brief Returns the canonical transaction identity. */
    [[nodiscard]] const eve::TransactionId& identity() const noexcept;
    /** @brief Returns the current lifecycle state. */
    State state() const;
    /** @brief Returns the correlation identifier supplied at creation. */
    const std::string& correlation() const;
    /** @brief Returns the causation identifier supplied at creation. */
    const std::string& causation() const;
    /**
     * @brief Adds an inert operation with a UUID-backed identity.
     * @param kind Stable operation kind.
     * @param target Domain target projection.
     * @param payloadJson Valid JSON payload copied into the plan.
     * @param operationId Explicit non-nil identity, or nil to use this ledger's injected UUID source.
     * @return The canonical operation identity, or a structured failure. No incrementing string ID is generated.
     */
    [[nodiscard]] eve::Result<eve::OperationId> stage(
        const std::string& kind, const std::string& target, const std::string& payloadJson = "null",
        eve::OperationId operationId = {});
    /** @brief Records a successful validation result for an operation. */
    [[nodiscard]] eve::Result<void> markValid(const std::string& operationId);
    /** @brief Records a successful validation result for a canonical operation. */
    [[nodiscard]] eve::Result<void> markValid(eve::OperationId operationId);
    /** @brief Records a failed validation result and its diagnostic. */
    [[nodiscard]] eve::Result<void> markInvalid(const std::string& operationId, const std::string& error);
    /** @brief Records a failed validation result for a canonical operation. */
    [[nodiscard]] eve::Result<void> markInvalid(eve::OperationId operationId, const std::string& error);
    /** @brief Enters validated state only when every operation was checked and valid. */
    [[nodiscard]] eve::Result<void> validate();
    /** @brief Commits a validated plan without executing its inert operations. */
    [[nodiscard]] eve::Result<void> commit();
    /** @brief Rolls back an open or validated plan with an optional reason. */
    [[nodiscard]] eve::Result<void> rollback(const std::string& reason = {});
    /** @brief Marks a non-terminal plan failed with a diagnostic. */
    [[nodiscard]] eve::Result<void> fail(const std::string& error);
    /** @brief Returns the latest plan-level error or terminal reason. */
    const std::string& error() const;
    /** @brief Returns the number of staged operations. */
    int operationCount() const;
    /**
     * @brief Returns an operation in staging order, or null for an invalid index.
     * @return Borrowed nullable read-only pointer owned by this plan.
     * @ownership The Plan owns its operations; callers must not delete or retain the pointer as an identity.
     * @lifetime Valid until the plan is destroyed or restored; use OperationId for cross-call identity.
     * @thread Call on the plan's owning transaction thread.
     * @reentrancy Do not retain across callbacks or plan mutation.
     */
    [[nodiscard]] const Operation* operationAt(int index) const;
    /**
     * @brief Returns an operation by stable identifier, or null when absent.
     * @return Borrowed nullable read-only pointer owned by this plan.
     * @ownership The Plan owns the operation; callers never release the result.
     * @lifetime Valid until the plan is destroyed or restored; use OperationId for cross-call identity.
     * @thread Call on the plan's owning transaction thread.
     * @reentrancy Do not retain across callbacks or plan mutation.
     */
    [[nodiscard]] const Operation* findOperation(const std::string& operationId) const;
    /**
     * @brief Returns an operation by canonical identity, or null when absent.
     * @return Borrowed nullable read-only pointer owned by this plan.
     * @ownership The Plan owns the operation; callers never release the result.
     * @lifetime Valid until the plan is destroyed or restored; use OperationId for cross-call identity.
     * @thread Call on the plan's owning transaction thread.
     * @reentrancy Do not retain across callbacks or plan mutation.
     */
    [[nodiscard]] const Operation* findOperation(eve::OperationId operationId) const;
    /** @brief Returns the number of deterministic events. */
    int eventCount() const;
    /**
     * @brief Returns an event in sequence order, or null for an invalid index.
     * @return Borrowed nullable read-only pointer owned by this plan's event log.
     * @ownership The Plan owns retained events; callers must not delete the result.
     * @lifetime Valid until the plan is destroyed or restored; use event sequence for identity.
     * @thread Call on the plan's owning transaction thread.
     * @reentrancy Do not retain across callbacks or plan mutation.
     */
    [[nodiscard]] const Event* eventAt(int index) const;
    /** @brief Exports this plan as deterministic compact JSON. */
    std::string snapshotJson() const;

private:
    friend class Ledger;
    Plan(std::string id, std::string correlation, std::string causation,
         eve::TransactionId identity = {}, eve::UuidEntropySource operationEntropy = {},
         eve::UuidClock operationClock = {});
    void emit(const std::string& type, const std::string& operationId = {}, const std::string& detail = {});

    std::string           id_;
    eve::TransactionId    identity_;
    State                 state_ = State::Open;
    std::string           correlation_;
    std::string           causation_;
    std::string           error_;
    uint64_t              nextOperation_ = 1;
    uint64_t              nextEvent_     = 1;
    std::deque<Operation> operations_;
    std::deque<Event>     events_;
    std::optional<eve::UuidV7Generator> operationIdGenerator_;
};

/** @brief Deterministic owner and identifier allocator for transaction plans. */
class Ledger {
public:
    /** @brief Creates an empty ledger with an optional persistent identity. */
    explicit Ledger(eve::PersistentId instanceId = {}, eve::UuidEntropySource transactionEntropy = {},
                    eve::UuidClock transactionClock = {});
    /**
     * @brief Creates an open plan with a UUID-backed identity.
     * @param correlation Stable business-chain projection.
     * @param causation Stable causing-command/event projection.
     * @param identity Explicit non-nil identity, or nil to use the injected UUID source.
     * @return A borrowed plan owned by this ledger, or a structured failure.
     */
    [[nodiscard]] eve::Result<Plan*> create(
        const std::string& correlation = {}, const std::string& causation = {},
        eve::TransactionId identity = {});
    /** @brief Returns a retained plan by legacy projection, or nullptr. */
    [[nodiscard]] Plan* find(const std::string& transactionId);
    /** @brief Returns a retained plan by canonical identity, or nullptr. */
    [[nodiscard]] Plan* find(eve::TransactionId transactionId);
    /** @brief Returns the number of retained plans. */
    int count() const;
    /** @brief Returns a plan in creation order, or nullptr. */
    [[nodiscard]] Plan* at(int index);
    /** @brief Exports all plans and allocator state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /**
     * @brief Transactionally restores a raw ledger snapshot.
     * @param json Snapshot produced by snapshotJson().
     * @return Success, or a structured failure that leaves this ledger unchanged.
     * @remarks This Result path is the single restoration implementation. The
     */
    [[nodiscard]] eve::Result<void> restore(std::string_view json);

    /**
     * @brief Captures the ledger payload in the common SnapshotEnvelope.
     * @param hashProvider Explicit content-digest provider.
     * @return A sealed transaction-ledger snapshot.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Restores a verified or migrated transaction-ledger snapshot atomically.
     * @param snapshot Source envelope with schema `transaction:ledger`.
     * @param hashProvider Explicit content-digest provider.
     * @return Success, or a failure leaving all ledger state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common transaction snapshot envelope. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common transaction snapshot envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(
        std::string_view json, const eve::SnapshotHashProvider& hashProvider);

private:
    uint64_t                           nextTransaction_ = 1;
    std::vector<std::unique_ptr<Plan>> plans_;
    eve::PersistentId                   instanceId_;
    eve::Revision                       revision_;
    eve::SimulationTick                 tick_;
    eve::UuidEntropySource              transactionEntropy_;
    eve::UuidClock                      transactionClock_;
    std::optional<eve::UuidV7Generator> transactionIdGenerator_;
};

/** @brief Returns the stable lowercase name of a transaction state. */
std::string stateName(State state);

/** @brief Script module factory for generic transaction ledgers. */
class Transaction : public Module {
public:
    Module_REG(Transaction);
    Transaction()           = default;
    ~Transaction() override = default;

    /**
     * @brief Allocates a module-owned transaction ledger.
     * @return Borrowed nullable pointer to the module-owned ledger; null means allocation failed.
     * @ownership The Transaction module owns the ledger and releases it during module teardown.
     * @lifetime Valid until explicit ledger release or Transaction module unload; use the ledger handle in asynchronous code.
     * @thread Call on the module's owning thread.
     * @reentrancy The factory does not invoke external callbacks; do not use the result across module teardown.
     */
    [[nodiscard("transaction ledger ownership must be retained or explicitly handled")]] static Ledger* newLedger();

private:
    std::vector<std::unique_ptr<Ledger>> ledgers_;
};

}  // namespace eve::transaction
