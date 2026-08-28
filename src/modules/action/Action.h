#pragma once

/**
 * @file Action.h
 * @brief Renderer- and ruleset-neutral gameplay action pipeline.
 *
 * An action owns one execution lifecycle. Domain adapters only translate a
 * Skill, Weapon Attack, Card Play, or RTS command into an ActionDefinition and
 * ActionRequest; they do not keep a second phase/timer state. Conditions,
 * targeting, resource accounts and active effects are injected ports so the
 * core does not depend on any particular gameplay domain. Resource
 * reservations are created only when Active is entered; validation performs
 * an affordability query and therefore never leaves a cross-frame reservation.
 */

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/ResourceAccount.h"
#include "common/Result.h"
#include "common/StrongUint64.h"
#include "common/Time.h"
#include "common/Value.h"
#include "decision/Condition.h"
#include "sensing/Targeting.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eve::action {

/** @brief Lifecycle phase shared by every gameplay action adapter. */
enum class ActionPhase : std::uint8_t {
    Requested,
    Validating,
    Windup,
    Active,
    Recover,
    Completed,
    Cancelled,
    Failed,
};

/**
 * @brief Return the stable lowercase spelling of an action phase.
 * @return Borrowed static storage valid for the lifetime of the process.
 * @borrowed The returned pointer must not be freed or retained as mutable data.
 */
[[nodiscard]] const char* actionPhaseName(ActionPhase phase) noexcept;

/** @brief Selects how an action obtains its target set. */
enum class TargetingMode : std::uint8_t {
    /** @brief The action has no target selection. */
    None,
    /** @brief The request already owns the selected ECS target handles. */
    Explicit,
    /** @brief The request supplies a sensing query resolved during validation. */
    Query,
};

/**
 * @brief Deterministic simulation durations for the three timed action phases.
 *
 * Durations are injected values. No action code reads a wall or monotonic
 * clock; the caller advances an execution with a SimulationTick and Duration.
 */
struct ActionTiming {
    /** @brief Time spent in Windup before Active. */
    Duration windup = Duration::zero();
    /** @brief Time for which the Active result remains in progress. */
    Duration active = Duration::zero();
    /** @brief Time spent in Recover before completion. */
    Duration recover = Duration::zero();
};

/**
 * @brief Immutable-by-convention definition of one action kind.
 *
 * The definition owns condition and metadata values. A cost is the canonical
 * common `CostSpec`; effectIds are references resolved by the injected active
 * executor. No RPG, RTS, Weapon or Card type is included here.
 */
struct ActionDefinition {
    /** @brief Scoped logical definition identity, not a runtime execution id. */
    LogicalId id;
    /** @brief Timed phase policy in simulation durations. */
    ActionTiming timing;
    /** @brief Side-effect-free precondition tree. */
    decision::Condition condition;
    /** @brief Target acquisition policy for this action. */
    TargetingMode targetingMode = TargetingMode::None;
    /** @brief Constraints used when targetingMode is Query. */
    std::optional<sensing::TargetingSpec> targetingSpec;
    /** @brief Optional canonical cost reserved and committed by the pipeline. */
    std::optional<resource::CostSpec> cost;
    /** @brief Stable effect-definition ids interpreted by the active executor. */
    std::vector<std::string> effectIds;
    /** @brief Whether Active must call the injected executor even with no effect ids. */
    bool activeExecutionRequired = false;
    /** @brief Deterministic extension data owned by this definition. */
    Value::Object metadata;

    /**
     * @brief Validate definition invariants without changing the definition.
     * @return Success or a structured rejection diagnostic.
     */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief One caller-owned request to instantiate an action execution.
 *
 * ECS handles are generation-checked by the ECS boundary when a domain
 * executor resolves them. The request stores handles, never long-lived raw
 * pointers. `requestedTick` is the deterministic starting point for this
 * execution.
 */
struct ActionRequest {
    /** @brief Definition identity this request intends to execute. */
    LogicalId actionId;
    /** @brief Optional source ECS identity; the action core does not interpret its type. */
    std::optional<ecs::EntityHandle> source;
    /** @brief Explicit targets, used only when the definition selects Explicit. */
    std::vector<ecs::EntityHandle> targetEntities;
    /** @brief Query input, used only when the definition selects Query. */
    std::optional<sensing::TargetingQuery> targetingQuery;
    /** @brief Adapter-owned deterministic parameters copied into the execution. */
    Value::Object parameters;
    /**
     * @brief Optional caller-supplied transaction correlation identifier.
     * @remarks Empty derives a stable local id from the action identity and
     * execution id. Non-empty values must be unique for the active operation.
     */
    std::string transactionId;
    /** @brief Simulation tick at which the request entered the pipeline. */
    SimulationTick requestedTick = SimulationTick::zero();

    /**
     * @brief Validate request/definition correspondence without side effects.
     * @param definition Definition selected by the caller.
     * @return Success or a structured rejection diagnostic.
     */
    [[nodiscard]] Result<void> validate(const ActionDefinition& definition) const;
};

/** @brief Opaque runtime identity for one ActionExecution. */
struct ActionExecutionIdTag {};
using ActionExecutionId = eve::detail::StrongUint64<ActionExecutionIdTag>;

/** @brief One phase change observed while advancing an action. */
struct ActionTransition {
    /** @brief Phase before the transition. */
    ActionPhase from = ActionPhase::Requested;
    /** @brief Phase after the transition. */
    ActionPhase to = ActionPhase::Requested;
    /** @brief Injected simulation tick at which the transition was applied. */
    SimulationTick tick = SimulationTick::zero();
};

/**
 * @brief Result payload for one deterministic advance call.
 *
 * The payload owns transition records. The execution remains owned by
 * ActionRuntime and must be queried through `find()` for its current state.
 */
struct ActionAdvance {
    /** @brief Execution advanced by this call. */
    ActionExecutionId id;
    /** @brief Current phase after all transitions in this call. */
    ActionPhase phase = ActionPhase::Requested;
    /** @brief Phase changes in deterministic order. */
    std::vector<ActionTransition> transitions;
    /** @brief Current time spent in the phase. */
    Duration phaseElapsed = Duration::zero();
    /** @brief Total simulation duration consumed by the execution. */
    Duration totalElapsed = Duration::zero();
};

/**
 * @brief Read-only condition composition port.
 *
 * Implementations synchronously evaluate the definition's Condition against
 * domain state. They must not mutate state, retain request references, or
 * invoke unknown callbacks while holding a lock. The provider is borrowed by
 * ActionRuntime and must outlive it on the same simulation thread.
 */
class IActionConditionEvaluator {
public:
    virtual ~IActionConditionEvaluator() = default;

    /**
     * @brief Evaluate one action condition.
     * @return ConditionResult or a structured provider failure.
     */
    [[nodiscard]] virtual Result<decision::ConditionResult> evaluate(const ActionDefinition& definition,
                                                                     const ActionRequest&    request) const = 0;
};

/**
 * @brief Targeting composition port.
 *
 * The resolver returns the existing sensing::TargetSet value type. It does
 * not select a target implicitly and must not retain the borrowed query.
 */
class IActionTargetResolver {
public:
    virtual ~IActionTargetResolver() = default;

    /**
     * @brief Resolve a validated targeting query.
     * @return An owning TargetSet or a structured target failure.
     */
    [[nodiscard]] virtual Result<sensing::TargetSet> resolve(const sensing::TargetingQuery& query) const = 0;
};

/**
 * @brief Resource-account lookup port for canonical CostSpec operations.
 *
 * The returned account is borrowed only for the immediate call sequence. The
 * runtime deliberately does not store its raw pointer across frames; an
 * adapter must return the same authoritative account for a request when
 * commit or rollback is later requested.
 */
class IActionResourceProvider {
public:
    virtual ~IActionResourceProvider() = default;

    /**
     * @brief Query affordability without reserving or mutating an account.
     * @param definition Action definition being validated.
     * @param request Original action request.
     * @param cost Canonical cost borrowed for this call.
     * @return Affordability or a structured provider failure.
     */
    [[nodiscard]] virtual Result<resource::Affordability> canAfford(const ActionDefinition&   definition,
                                                                    const ActionRequest&      request,
                                                                    const resource::CostSpec& cost) const = 0;

    /**
     * @brief Reserve a cost at the Active boundary.
     * @param definition Action definition being activated.
     * @param request Original action request.
     * @param cost Canonical cost borrowed for this call.
     * @return Reservation credential or a failure with no mutation.
     * @remarks A successful reservation creates a nonce route owned by this
     *          provider. Changing the provider's default account, replacing
     *          an account in the current selection, or unloading that default
     *          account must not discard the route before commit/rollback.
     */
    [[nodiscard]] virtual Result<resource::Reservation> reserve(const ActionDefinition&   definition,
                                                                const ActionRequest&      request,
                                                                const resource::CostSpec& cost) = 0;

    /**
     * @brief Commit a reservation by its account nonce.
     * @param reservation Credential returned by reserve.
     * @return Debit receipt or a structured failure.
     * @remarks Implementations must route by reservation.account, not by the
     *          current/default account for the request. An unsuccessful commit
     *          must leave the reservation rollback-able; a provider must not
     *          report failure after exposing a debit without compensation.
     */
    [[nodiscard]] virtual Result<resource::Receipt> commit(const resource::Reservation& reservation) = 0;

    /**
     * @brief Roll back a reservation by its account nonce.
     * @param reservation Credential returned by reserve.
     * @return Applied or a structured failure.
     * @remarks Implementations must use the same nonce route as commit, even
     *          if the provider's current account selection has changed or its
     *          default account has been unloaded. An unknown nonce must fail
     *          explicitly and must never fall back to the current account.
     */
    [[nodiscard]] virtual Result<void> rollback(const resource::Reservation& reservation) = 0;
};

/**
 * @brief Move-only staged Active operation.
 *
 * A provider prepares all validation and external allocations before commit.
 * `commit` and `rollback` are noexcept so the action coordinator can finish
 * its transaction without a second fallible boundary. Implementations must
 * make rollback safe after a prepare that has not been committed.
 */
class IActionEffectOperation {
public:
    virtual ~IActionEffectOperation() = default;

    IActionEffectOperation(const IActionEffectOperation&)            = delete;
    IActionEffectOperation& operator=(const IActionEffectOperation&) = delete;
    IActionEffectOperation(IActionEffectOperation&&)                 = delete;
    IActionEffectOperation& operator=(IActionEffectOperation&&)      = delete;

    /** @brief Apply the prepared operation; must not fail or throw. */
    virtual void commit() noexcept = 0;
    /** @brief Release/compensate the prepared operation; must not fail or throw. */
    virtual void rollback() noexcept = 0;

protected:
    IActionEffectOperation() = default;
};

/**
 * @brief Active-phase effect/operation preparation port.
 *
 * Effect IDs are resolved by the provider against the canonical effects
 * registry/container. A Weapon adapter can use the same port for its Active
 * fire operation while preserving weapon-specific state in the weapon
 * module. `targets` is borrowed for this call only. The returned unique_ptr is
 * the sole owner of staged external state until ActionRuntime commits or
 * rolls it back.
 */
class IActionEffectExecutor {
public:
    virtual ~IActionEffectExecutor() = default;

    /**
     * @brief Prepare the active operation once without applying its effect.
     * @param definition Action definition containing effect references.
     * @param request Original owning request.
     * @param targets Resolved target set, or null when no target was selected.
     * @param tick Deterministic simulation tick of the Active transition.
     * @return A move-only staged operation or a structured preparation failure.
     */
    [[nodiscard]] virtual Result<std::unique_ptr<IActionEffectOperation>> prepare(const ActionDefinition&   definition,
                                                                                  const ActionRequest&      request,
                                                                                  const sensing::TargetSet* targets,
                                                                                  SimulationTick            tick) = 0;
};

/** @brief Borrowed composition ports used by one ActionRuntime owner. */
struct ActionServices {
    /** @brief Optional precondition evaluator; empty is valid for an empty Condition. */
    IActionConditionEvaluator* conditions = nullptr;
    /** @brief Optional query resolver; required by Query actions. */
    IActionTargetResolver* targeting = nullptr;
    /** @brief Optional account provider; required by cost-bearing actions. */
    IActionResourceProvider* resources = nullptr;
    /** @brief Optional active executor; required by effects/active operations. */
    IActionEffectExecutor* effects = nullptr;
    /**
     * @brief Optional common transaction effect participant.
     *
     * When set, Active routes this participant and `transactionAccount`
     * through AtomicResourcePayment. This is the bridge for domain adapters
     * that already own a fallible prepare/commit/compensate contract.
     */
    transaction::ITransactionParticipant* transactionEffect = nullptr;
    /** @brief Account paired with transactionEffect for a cost-bearing action. */
    resource::IResourceAccount* transactionAccount = nullptr;
};

/**
 * @brief Sole owner of all ActionExecution lifecycle state.
 *
 * Instances are not thread-safe. One simulation/owner thread must serialize
 * submit, advance, cancel and service callbacks. ActionRuntime owns executions
 * until erased by a future explicit retention policy; pointers returned by
 * `find()` are borrowed and are invalidated by runtime destruction.
 */
class ActionRuntime;

/**
 * @brief Read-only view of one execution owned by ActionRuntime.
 *
 * Adapters receive no mutable lifecycle fields and must not create a second
 * timer or phase machine. The optional receipt is an audit value; the
 * resource provider/account remains authoritative for resource state. A
 * reservation is never retained across an action phase or frame.
 */
class ActionExecution {
public:
    /** @brief Runtime identity. */
    [[nodiscard]] ActionExecutionId id() const noexcept { return id_; }
    /** @brief Definition identity copied at submission time. */
    [[nodiscard]] const LogicalId& definitionId() const noexcept { return definition_.id; }
    /** @brief Original definition owned by this execution. */
    [[nodiscard]] const ActionDefinition& definition() const noexcept { return definition_; }
    /** @brief Original request owned by this execution. */
    [[nodiscard]] const ActionRequest& request() const noexcept { return request_; }
    /** @brief Current lifecycle phase. */
    [[nodiscard]] ActionPhase phase() const noexcept { return phase_; }
    /** @brief Time spent in the current phase. */
    [[nodiscard]] Duration phaseElapsed() const noexcept { return phaseElapsed_; }
    /** @brief Total injected simulation duration consumed. */
    [[nodiscard]] Duration totalElapsed() const noexcept { return totalElapsed_; }
    /** @brief Last simulation tick observed by the execution. */
    [[nodiscard]] SimulationTick lastTick() const noexcept { return lastTick_; }
    /** @brief Current structured execution status. */
    [[nodiscard]] const Status& status() const noexcept { return status_; }
    /**
     * @brief Resolved targets, or null until a Query action is validated.
     * @return Borrowed storage owned by this ActionExecution; valid until the
     *         execution is erased or its ActionRuntime is destroyed.
     * @lifetime The pointer is observation-only and must not be retained across
     *           ActionRuntime ownership changes.
     */
    [[nodiscard]] const sensing::TargetSet* resolvedTargets() const noexcept {
        return resolvedTargets_ ? &*resolvedTargets_ : nullptr;
    }
    /** @brief Whether Active committed a resource debit. */
    [[nodiscard]] bool hasCommittedCost() const noexcept { return receipt_.has_value(); }
    /** @brief Whether the Active transaction has committed successfully. */
    [[nodiscard]] bool activeExecuted() const noexcept { return activeExecuted_; }
    /**
     * @brief Return the common transaction receipt produced at Active.
     * @return A borrowed receipt, or null when the legacy effect port was used.
     * @lifetime Valid until this ActionExecution is erased or its ActionRuntime
     *           is destroyed; the caller must not retain it beyond that owner.
     */
    [[nodiscard]] const transaction::TransactionReceipt* transactionReceipt() const noexcept {
        return transactionReceipt_ ? &*transactionReceipt_ : nullptr;
    }

private:
    friend class ActionRuntime;

    ActionExecution(ActionExecutionId id, ActionDefinition definition, ActionRequest request)
        : id_(id),
          definition_(std::move(definition)),
          request_(std::move(request)),
          lastTick_(request_.requestedTick),
          status_(Status::success(StatusCode::Pending)) {}

    ActionExecutionId                              id_;
    ActionDefinition                               definition_;
    ActionRequest                                  request_;
    ActionPhase                                    phase_        = ActionPhase::Requested;
    Duration                                       phaseElapsed_ = Duration::zero();
    Duration                                       totalElapsed_ = Duration::zero();
    SimulationTick                                 lastTick_     = SimulationTick::zero();
    Status                                         status_;
    std::optional<sensing::TargetSet>              resolvedTargets_;
    std::optional<resource::Receipt>               receipt_;
    std::optional<transaction::TransactionReceipt> transactionReceipt_;
    bool                                           activeExecuted_ = false;
};

/**
 * @brief Reusable adapter from the sensing TargetingResolver to ActionRuntime.
 *
 * It owns no target state and is safe to share only under the resolver's
 * documented owner-thread contract. Capability lookup remains inside sensing.
 */
class SensingTargetingAdapter final : public IActionTargetResolver {
public:
    /** @brief Construct an adapter with no retained query or target state. */
    SensingTargetingAdapter() = default;

    /** @copydoc IActionTargetResolver::resolve */
    [[nodiscard]] Result<sensing::TargetSet> resolve(const sensing::TargetingQuery& query) const override;

private:
    sensing::TargetingResolver resolver_;
};

/**
 * @brief Deterministic action lifecycle coordinator.
 *
 * No method reads a clock. `advance()` consumes only the supplied tick and
 * duration. Structural execution storage is owned here; adapters only submit
 * requests and observe results.
 */
class ActionRuntime {
public:
    /**
     * @brief Bind borrowed composition ports to this owner-thread runtime.
     * @param services Providers used synchronously during validation and Active.
     * @remarks Every non-null provider must outlive this runtime and obey its
     *          own thread/reentrancy contract.
     */
    explicit ActionRuntime(ActionServices services = {}) : services_(services) {}

    ActionRuntime(const ActionRuntime&)            = delete;
    ActionRuntime& operator=(const ActionRuntime&) = delete;
    ActionRuntime(ActionRuntime&&)                 = delete;
    ActionRuntime& operator=(ActionRuntime&&)      = delete;
    ~ActionRuntime()                               = default;

    /**
     * @brief Submit a validated definition/request pair in Requested phase.
     * @param definition Definition copied into the execution.
     * @param request Request copied into the execution.
     * @return New execution id or a structured rejection; no state is created
     *         when validation fails.
     */
    [[nodiscard]] Result<ActionExecutionId> submit(ActionDefinition definition, ActionRequest request);

    /**
     * @brief Advance one execution by an injected simulation duration.
     * @param id Execution identity returned by submit.
     * @param tick Non-decreasing simulation tick for this call.
     * @param delta Non-negative simulation duration; zero is allowed.
     * @return Transition summary, or a structured failure. A failed/cancelled
     *         execution is observable through find() after the failure.
     */
    [[nodiscard]] Result<ActionAdvance> advance(ActionExecutionId id, SimulationTick tick, Duration delta);

    /**
     * @brief Cancel a non-terminal execution before its Active transaction.
     * @param id Execution identity returned by submit.
     * @param tick Deterministic simulation tick at cancellation.
     * @return Applied/NoOp operation result, or a structured cancellation
     *         failure. No reservation exists across frames; after Active has
     *         committed, cancellation does not undo the committed operation.
     */
    [[nodiscard]] Result<void> cancel(ActionExecutionId id, SimulationTick tick);

    /**
     * @brief Find a borrowed read-only execution view, or null when absent.
     * @return Borrowed storage owned by this runtime; valid until erase or
     *         ActionRuntime destruction.
     * @lifetime The caller must not retain the pointer beyond that owner.
     */
    [[nodiscard]] const ActionExecution* find(ActionExecutionId id) const noexcept;
    /**
     * @brief Find a borrowed mutable execution view for owner-side inspection.
     * @return Borrowed storage owned by this runtime; valid until erase or
     *         ActionRuntime destruction.
     * @lifetime The caller must not retain the pointer beyond that owner.
     */
    [[nodiscard]] ActionExecution* find(ActionExecutionId id) noexcept;
    /** @brief Number of executions retained by this runtime. */
    [[nodiscard]] std::size_t executionCount() const noexcept { return executions_.size(); }

private:
    using ExecutionStore = std::map<ActionExecutionId, std::unique_ptr<ActionExecution>>;

    [[nodiscard]] Result<void> validateExecution(ActionExecution& execution);
    [[nodiscard]] Result<void> enterActive(ActionExecution& execution, SimulationTick tick);
    [[nodiscard]] Result<void> addElapsed(ActionExecution& execution, Duration amount);
    void                       transition(ActionExecution& execution, ActionPhase next, SimulationTick tick,
                                          std::vector<ActionTransition>& transitions);
    void                       failExecution(ActionExecution& execution, Status status, SimulationTick tick,
                                             std::vector<ActionTransition>* transitions = nullptr);
    [[nodiscard]] Result<ActionExecutionId> nextExecutionId();

    ActionServices    services_;
    ExecutionStore    executions_;
    ActionExecutionId nextId_{1};
};

}  // namespace eve::action
