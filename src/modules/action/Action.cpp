#include "action/Action.h"

#include "transaction/AtomicResourcePayment.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace eve::action {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failureFrom(const Status& status) { return Result<void>::failure(status); }

template <class T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool isEmptyCondition(const decision::Condition& condition) {
    return condition.kind() == decision::ConditionKind::All && condition.children().empty() && condition.isValid();
}

bool isTerminal(ActionPhase phase) {
    return phase == ActionPhase::Completed || phase == ActionPhase::Cancelled || phase == ActionPhase::Failed;
}

Status cancelledStatus() {
    return Status::failure(StatusCode::Cancelled,
                           Diagnostic::error(DiagnosticCode::Cancelled, "action execution was cancelled", "action"));
}

Status notFoundStatus() {
    return Status::failure(StatusCode::NotFound,
                           Diagnostic::error(DiagnosticCode::NotFound, "action execution was not found", "execution"));
}

Status invalidStatus(std::string message, std::string path = {}) {
    return Status::failure(StatusCode::Rejected,
                           Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Status pendingStatus() { return Status::success(StatusCode::Pending); }

}  // namespace

const char* actionPhaseName(ActionPhase phase) noexcept {
    switch (phase) {
        case ActionPhase::Requested: return "requested";
        case ActionPhase::Validating: return "validating";
        case ActionPhase::Windup: return "windup";
        case ActionPhase::Active: return "active";
        case ActionPhase::Recover: return "recover";
        case ActionPhase::Completed: return "completed";
        case ActionPhase::Cancelled: return "cancelled";
        case ActionPhase::Failed: return "failed";
    }
    return "unknown";
}

Result<void> ActionDefinition::validate() const {
    if (!id.isValid()) return failure(DiagnosticCode::InvalidArgument, "action definition id is invalid", "id");
    if (!condition.isValid())
        return failure(DiagnosticCode::InvalidArgument, "action condition is invalid", "condition");
    if (timing.windup.nanoseconds() < 0 || timing.active.nanoseconds() < 0 || timing.recover.nanoseconds() < 0)
        return failure(DiagnosticCode::InvalidArgument, "action phase durations must be non-negative", "timing");

    switch (targetingMode) {
        case TargetingMode::None:
        case TargetingMode::Explicit:
            if (targetingSpec)
                return failure(DiagnosticCode::InvalidArgument, "targetingSpec is only valid for Query actions",
                               "targetingSpec");
            break;
        case TargetingMode::Query:
            if (!targetingSpec)
                return failure(DiagnosticCode::InvalidArgument, "Query actions require a targetingSpec",
                               "targetingSpec");
            {
                auto valid = targetingSpec->validate();
                if (!valid) return failureFrom(valid.status());
            }
            break;
    }

    if (cost && !cost->isValid())
        return failure(DiagnosticCode::InvalidArgument, "action cost must be a validated non-empty CostSpec", "cost");
    for (const auto& effectId : effectIds) {
        if (effectId.empty())
            return failure(DiagnosticCode::InvalidArgument, "action effect ids must not be empty", "effectIds");
    }
    if (timeline) {
        auto timelineValid = timeline->validate();
        if (!timelineValid) return failureFrom(timelineValid.status());
        if (timeline->actionId != id)
            return failure(DiagnosticCode::InvalidArgument, "action timeline id does not match its definition",
                           "timeline.actionId");
        auto windupAndActive = timing.windup.tryAdd(timing.active);
        if (!windupAndActive) return failureFrom(windupAndActive.status());
        auto total = windupAndActive.value().tryAdd(timing.recover);
        if (!total) return failureFrom(total.status());
        if (timeline->duration != total.value())
            return failure(DiagnosticCode::InvalidArgument,
                           "action timeline duration must equal windup + active + recover", "timeline.durationNs");
    }
    return Result<void>::success();
}

Result<void> ActionRequest::validate(const ActionDefinition& definition) const {
    auto definitionValid = definition.validate();
    if (!definitionValid) return failureFrom(definitionValid.status());
    if (actionId != definition.id)
        return failure(DiagnosticCode::InvalidArgument, "action request id does not match its definition", "actionId");

    switch (definition.targetingMode) {
        case TargetingMode::None:
            if (!targetEntities.empty() || targetingQuery)
                return failure(DiagnosticCode::InvalidArgument, "a non-targeted action cannot carry target selection",
                               "targets");
            break;
        case TargetingMode::Explicit:
            if (targetEntities.empty())
                return failure(DiagnosticCode::PreconditionViolation, "an Explicit action requires at least one target",
                               "targetEntities");
            if (targetingQuery)
                return failure(DiagnosticCode::InvalidArgument, "Explicit actions cannot carry a targeting query",
                               "targetingQuery");
            break;
        case TargetingMode::Query:
            if (!targetingQuery)
                return failure(DiagnosticCode::PreconditionViolation, "a Query action requires a targeting query",
                               "targetingQuery");
            if (!targetEntities.empty())
                return failure(DiagnosticCode::InvalidArgument, "Query actions cannot carry explicit targets",
                               "targetEntities");
            {
                auto valid = targetingQuery->validate();
                if (!valid) return failureFrom(valid.status());
            }
            break;
    }
    return Result<void>::success();
}

Result<sensing::TargetSet> SensingTargetingAdapter::resolve(const sensing::TargetingQuery& query) const {
    return resolver_.resolve(query);
}

Result<ActionExecutionId> ActionRuntime::nextExecutionId() {
    if (nextId_.isZero())
        return failure<ActionExecutionId>(DiagnosticCode::InvariantViolation, "action execution id is zero",
                                          "execution.id");
    const ActionExecutionId id   = nextId_;
    const auto              next = nextId_.incremented();
    if (!next)
        return failure<ActionExecutionId>(DiagnosticCode::InvariantViolation, "action execution id exhausted",
                                          "execution.id");
    nextId_ = *next;
    return Result<ActionExecutionId>::success(id);
}

Result<ActionExecutionId> ActionRuntime::submit(ActionDefinition definition, ActionRequest request) {
    auto definitionValid = definition.validate();
    if (!definitionValid) return failureFrom<ActionExecutionId>(definitionValid.status());
    auto requestValid = request.validate(definition);
    if (!requestValid) return failureFrom<ActionExecutionId>(requestValid.status());

    auto idResult = nextExecutionId();
    if (!idResult) return failureFrom<ActionExecutionId>(idResult.status());
    const ActionExecutionId id = std::move(idResult).takeValue();
    // Construct inside this friend member rather than using make_unique:
    // ActionExecution's constructor is intentionally private so adapters
    // cannot manufacture lifecycle owners.
    std::unique_ptr<ActionExecution> execution(new ActionExecution(id, std::move(definition), std::move(request)));
    const auto [it, inserted] = executions_.emplace(id, std::move(execution));
    if (!inserted)
        return failure<ActionExecutionId>(DiagnosticCode::Conflict, "action execution id collided", "execution.id");
    return Result<ActionExecutionId>::success(it->first, Status::success(StatusCode::Pending));
}

const ActionExecution* ActionRuntime::find(ActionExecutionId id) const noexcept {
    const auto it = executions_.find(id);
    return it == executions_.end() ? nullptr : it->second.get();
}

ActionExecution* ActionRuntime::find(ActionExecutionId id) noexcept {
    const auto it = executions_.find(id);
    return it == executions_.end() ? nullptr : it->second.get();
}

Result<void> ActionRuntime::validateExecution(ActionExecution& execution) {
    const auto& definition = execution.definition_;
    const auto& request    = execution.request_;

    if (!isEmptyCondition(definition.condition)) {
        if (services_.conditions == nullptr)
            return failure(DiagnosticCode::Unsupported, "action condition requires an evaluator",
                           "services.conditions");
        auto condition = services_.conditions->evaluate(definition, request);
        if (!condition) return failureFrom(condition.status());
        auto checked = std::move(condition).takeValue();
        if (!checked.passed()) {
            DiagnosticDetails details;
            details.emplace_back("reason", decision::conditionReasonCodeName(checked.reasonCode()));
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                           "action condition was rejected", "condition",
                                                           std::move(details)));
        }
    }

    if (definition.targetingMode == TargetingMode::Query) {
        if (services_.targeting == nullptr)
            return failure(DiagnosticCode::Unsupported, "Query action requires a target resolver",
                           "services.targeting");
        sensing::TargetingQuery query = *request.targetingQuery;
        query.spec                    = *definition.targetingSpec;
        auto targets                  = services_.targeting->resolve(query);
        if (!targets) return failureFrom(targets.status());
        execution.resolvedTargets_ = std::move(targets).takeValue();
    }

    if (definition.activeExecutionRequired || !definition.effectIds.empty()) {
        if (services_.effects == nullptr && services_.transactionEffect == nullptr)
            return failure(DiagnosticCode::Unsupported, "action Active phase requires an effect executor",
                           "services.effects");
    }

    if (definition.cost) {
        const bool transactionBacked =
            services_.transactionEffect != nullptr && services_.transactionAccount != nullptr;
        if (services_.resources == nullptr && !transactionBacked)
            return failure(DiagnosticCode::Unsupported, "cost-bearing action requires a resource provider",
                           "services.resources");
        auto affordability = services_.resources != nullptr
                                 ? services_.resources->canAfford(definition, request, *definition.cost)
                                 : services_.transactionAccount->canAfford(*definition.cost);
        if (!affordability) return failureFrom(affordability.status());
        auto checked = std::move(affordability).takeValue();
        if (!checked.affordable) {
            DiagnosticDetails details;
            for (const auto& shortfall : checked.shortfalls) {
                details.emplace_back("resource", shortfall.resource.value());
                details.emplace_back("required", std::to_string(shortfall.required.value()));
                details.emplace_back("available", std::to_string(shortfall.available.value()));
            }
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                           "action resource cost is not affordable", "cost",
                                                           std::move(details)));
        }
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionRuntime::enterActive(ActionExecution& execution, SimulationTick tick) {
    const auto& definition = execution.definition_;
    const auto& request    = execution.request_;

    if (services_.transactionEffect != nullptr) {
        if (definition.cost && services_.transactionAccount == nullptr)
            return failure(DiagnosticCode::Unsupported, "transaction-backed action cost requires an account",
                           "services.transactionAccount");

        const std::string transactionId = request.transactionId.empty() ? "action." + definition.id.format() + "." +
                                                                              std::to_string(execution.id_.value())
                                                                        : request.transactionId;
        transaction::TransactionContext context(transactionId);
        auto                            transactionResult =
            definition.cost
                                           ? transaction::AtomicResourcePayment::execute(context, *services_.transactionAccount, *definition.cost,
                                                                                         *services_.transactionEffect)
                                           : transaction::AtomicResourcePayment::execute(context, *services_.transactionEffect);
        if (!transactionResult) return failureFrom(transactionResult.status());
        execution.transactionReceipt_ = std::move(transactionResult).takeValue();
        execution.activeExecuted_     = true;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    std::unique_ptr<IActionEffectOperation> stagedEffect;
    if (definition.activeExecutionRequired || !definition.effectIds.empty()) {
        if (services_.effects == nullptr)
            return failure(DiagnosticCode::Unsupported, "action Active phase requires an effect executor",
                           "services.effects");
        auto prepared = services_.effects->prepare(
            definition, request, execution.resolvedTargets_ ? &*execution.resolvedTargets_ : nullptr, tick);
        if (!prepared) return failureFrom(prepared.status());
        stagedEffect = std::move(prepared).takeValue();
        if (!stagedEffect)
            return failure(DiagnosticCode::InvariantViolation, "effect executor returned an empty staged operation",
                           "effects");
    }

    std::optional<resource::Reservation> reservation;
    if (definition.cost) {
        if (services_.resources == nullptr) {
            if (stagedEffect) stagedEffect->rollback();
            return failure(DiagnosticCode::Unsupported, "activating a cost-bearing action requires a resource provider",
                           "services.resources");
        }
        auto reserved = services_.resources->reserve(definition, request, *definition.cost);
        if (!reserved) {
            if (stagedEffect) stagedEffect->rollback();
            return failureFrom(reserved.status());
        }
        auto credential = std::move(reserved).takeValue();
        if (!credential.isValid()) {
            if (stagedEffect) stagedEffect->rollback();
            return failure(DiagnosticCode::InvariantViolation, "resource provider returned an invalid reservation",
                           "cost.reservation");
        }
        reservation = std::move(credential);

        // The provider routes this operation by reservation.account. The
        // action runtime never retains an account pointer, so a provider may
        // change its default/request routing without misdirecting rollback.
        auto committed = services_.resources->commit(*reservation);
        if (!committed) {
            auto rolledBack = services_.resources->rollback(*reservation);
            if (!rolledBack) {
                rolledBack.ignore("commit failed and rollback also failed; commit diagnostic retained");
            }
            if (stagedEffect) stagedEffect->rollback();
            return failureFrom(committed.status());
        }
        auto receipt       = std::move(committed).takeValue();
        execution.receipt_ = std::move(receipt);
    }

    if (stagedEffect) {
        // Resource commit is the only fallible commit boundary. Once it has
        // succeeded, the staged effect commit is noexcept and cannot leave a
        // successfully debited action without its Active operation.
        stagedEffect->commit();
    }
    // Mark the whole Active transaction as complete even for a timing-only or
    // cost-only action. Otherwise a zero-effect action would reserve/commit
    // its cost again on every subsequent advance while it remains Active.
    execution.activeExecuted_ = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionRuntime::addElapsed(ActionExecution& execution, Duration amount) {
    auto phase = execution.phaseElapsed_.tryAdd(amount);
    if (!phase) return failureFrom(phase.status());
    auto total = execution.totalElapsed_.tryAdd(amount);
    if (!total) return failureFrom(total.status());
    execution.phaseElapsed_ = std::move(phase).takeValue();
    execution.totalElapsed_ = std::move(total).takeValue();
    return Result<void>::success();
}

void ActionRuntime::transition(ActionExecution& execution, ActionPhase next, SimulationTick tick,
                               std::vector<ActionTransition>& transitions) {
    if (execution.phase_ == next) return;
    transitions.push_back({execution.phase_, next, tick});
    execution.phase_        = next;
    execution.phaseElapsed_ = Duration::zero();
    if (next == ActionPhase::Completed)
        execution.status_ = Status::success(StatusCode::Applied);
    else if (next == ActionPhase::Requested || next == ActionPhase::Validating || next == ActionPhase::Windup ||
             next == ActionPhase::Active || next == ActionPhase::Recover)
        execution.status_ = pendingStatus();
}

void ActionRuntime::failExecution(ActionExecution& execution, Status status, SimulationTick tick,
                                  std::vector<ActionTransition>* transitions) {
    if (status.isSuccess())
        status = Status::failure(StatusCode::Failed,
                                 Diagnostic::error(DiagnosticCode::Failed, "action execution failed", "action"));
    if (transitions && execution.phase_ != ActionPhase::Failed)
        transition(execution, ActionPhase::Failed, tick, *transitions);
    else
        execution.phase_ = ActionPhase::Failed;
    execution.status_ = std::move(status);
}

Result<ActionAdvance> ActionRuntime::advance(ActionExecutionId id, SimulationTick tick, Duration delta) {
    ActionExecution* execution = find(id);
    if (execution == nullptr) return failureFrom<ActionAdvance>(notFoundStatus());
    if (delta.nanoseconds() < 0)
        return failureFrom<ActionAdvance>(invalidStatus("action advance duration must be non-negative", "delta"));
    if (tick < execution->lastTick_)
        return failureFrom<ActionAdvance>(invalidStatus("action simulation tick moved backwards", "tick"));
    execution->lastTick_            = tick;
    const Duration timelinePrevious = execution->totalElapsed_;

    if (execution->phase_ == ActionPhase::Completed) {
        ActionAdvance result{id, execution->phase_, {}, execution->phaseElapsed_, execution->totalElapsed_};
        return Result<ActionAdvance>::success(std::move(result), Status::success(StatusCode::NoOp));
    }
    if (execution->phase_ == ActionPhase::Cancelled || execution->phase_ == ActionPhase::Failed)
        return failureFrom<ActionAdvance>(execution->status());

    Duration                      remaining = delta;
    std::vector<ActionTransition> transitions;
    for (int guard = 0; guard < 8; ++guard) {
        if (execution->phase_ == ActionPhase::Requested) {
            transition(*execution, ActionPhase::Validating, tick, transitions);
            auto valid = validateExecution(*execution);
            if (!valid) {
                failExecution(*execution, valid.status(), tick, &transitions);
                return failureFrom<ActionAdvance>(execution->status());
            }
            continue;
        }

        if (execution->phase_ == ActionPhase::Validating) {
            if (execution->definition_.timing.windup.isZero())
                transition(*execution, ActionPhase::Active, tick, transitions);
            else
                transition(*execution, ActionPhase::Windup, tick, transitions);
            continue;
        }

        if (execution->phase_ == ActionPhase::Windup) {
            const Duration phase  = execution->definition_.timing.windup;
            const auto     needed = phase.nanoseconds() - execution->phaseElapsed_.nanoseconds();
            if (remaining.nanoseconds() < needed) {
                auto added = addElapsed(*execution, remaining);
                if (!added) {
                    failExecution(*execution, added.status(), tick, &transitions);
                    return failureFrom<ActionAdvance>(execution->status());
                }
                remaining = Duration::zero();
                break;
            }
            auto added = addElapsed(*execution, Duration::fromNanoseconds(needed));
            if (!added) {
                failExecution(*execution, added.status(), tick, &transitions);
                return failureFrom<ActionAdvance>(execution->status());
            }
            remaining = Duration::fromNanoseconds(remaining.nanoseconds() - needed);
            transition(*execution, ActionPhase::Active, tick, transitions);
            continue;
        }

        if (execution->phase_ == ActionPhase::Active) {
            if (!execution->activeExecuted_) {
                auto active = enterActive(*execution, tick);
                if (!active) {
                    failExecution(*execution, active.status(), tick, &transitions);
                    return failureFrom<ActionAdvance>(execution->status());
                }
            }
            const Duration phase  = execution->definition_.timing.active;
            const auto     needed = phase.nanoseconds() - execution->phaseElapsed_.nanoseconds();
            if (needed == 0) {
                transition(*execution, ActionPhase::Recover, tick, transitions);
                continue;
            }
            if (remaining.nanoseconds() < needed) {
                auto added = addElapsed(*execution, remaining);
                if (!added) {
                    failExecution(*execution, added.status(), tick, &transitions);
                    return failureFrom<ActionAdvance>(execution->status());
                }
                remaining = Duration::zero();
                break;
            }
            auto added = addElapsed(*execution, Duration::fromNanoseconds(needed));
            if (!added) {
                failExecution(*execution, added.status(), tick, &transitions);
                return failureFrom<ActionAdvance>(execution->status());
            }
            remaining = Duration::fromNanoseconds(remaining.nanoseconds() - needed);
            transition(*execution, ActionPhase::Recover, tick, transitions);
            continue;
        }

        if (execution->phase_ == ActionPhase::Recover) {
            const Duration phase  = execution->definition_.timing.recover;
            const auto     needed = phase.nanoseconds() - execution->phaseElapsed_.nanoseconds();
            if (needed == 0) {
                transition(*execution, ActionPhase::Completed, tick, transitions);
                continue;
            }
            if (remaining.nanoseconds() < needed) {
                auto added = addElapsed(*execution, remaining);
                if (!added) {
                    failExecution(*execution, added.status(), tick, &transitions);
                    return failureFrom<ActionAdvance>(execution->status());
                }
                remaining = Duration::zero();
                break;
            }
            auto added = addElapsed(*execution, Duration::fromNanoseconds(needed));
            if (!added) {
                failExecution(*execution, added.status(), tick, &transitions);
                return failureFrom<ActionAdvance>(execution->status());
            }
            remaining = Duration::fromNanoseconds(remaining.nanoseconds() - needed);
            transition(*execution, ActionPhase::Completed, tick, transitions);
            continue;
        }

        if (isTerminal(execution->phase_)) break;
    }

    std::vector<ActionTimelineEvent> timelineEvents;
    if (execution->definition_.timeline) {
        auto sampled = execution->definition_.timeline->sample(timelinePrevious, execution->totalElapsed_,
                                                               !execution->timelineStarted_);
        if (!sampled) {
            failExecution(*execution, sampled.status(), tick, &transitions);
            return failureFrom<ActionAdvance>(execution->status());
        }
        timelineEvents              = std::move(sampled).takeValue();
        execution->timelineStarted_ = true;
    }
    if (execution->phase_ == ActionPhase::Completed) {
        ActionAdvance result{id,
                             execution->phase_,
                             std::move(transitions),
                             execution->phaseElapsed_,
                             execution->totalElapsed_,
                             std::move(timelineEvents)};
        return Result<ActionAdvance>::success(std::move(result), Status::success(StatusCode::Applied));
    }
    ActionAdvance result{id,
                         execution->phase_,
                         std::move(transitions),
                         execution->phaseElapsed_,
                         execution->totalElapsed_,
                         std::move(timelineEvents)};
    return Result<ActionAdvance>::success(std::move(result), pendingStatus());
}

Result<void> ActionRuntime::cancel(ActionExecutionId id, SimulationTick tick) {
    ActionExecution* execution = find(id);
    if (execution == nullptr) return Result<void>::failure(notFoundStatus());
    if (execution->phase_ == ActionPhase::Completed)
        return Result<void>::failure(Status::failure(
            StatusCode::Conflict,
            Diagnostic::error(DiagnosticCode::Conflict, "completed action execution cannot be cancelled", "action")));
    if (execution->phase_ == ActionPhase::Failed) return Result<void>::failure(execution->status_);
    if (execution->phase_ == ActionPhase::Cancelled) return Result<void>::success(Status::success(StatusCode::NoOp));

    execution->phase_        = ActionPhase::Cancelled;
    execution->phaseElapsed_ = Duration::zero();
    execution->lastTick_     = tick;
    execution->status_       = cancelledStatus();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ActionRuntime::clear() noexcept {
    executions_.clear();
    nextId_ = ActionExecutionId{1};
}

}  // namespace eve::action
