#include "production/Production.h"

#include "common/Json.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace eve::production {
namespace {

template <class T>
eve::Result<T> productionBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "production.squirrel"));
}

bool terminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Cancelled || state == TaskState::Failed;
}

struct ScaledWork {
    std::int64_t appliedNanoseconds = 0;
    std::uint32_t remainderPermille = 0;
};

eve::Result<ScaledWork> scaleWork(std::int64_t effortNanoseconds, std::uint32_t efficiencyPermille,
                                  std::uint32_t remainderPermille) {
    if (effortNanoseconds < 0 || efficiencyPermille == 0 || remainderPermille >= 1000)
        return productionBindingFailure<ScaledWork>(eve::DiagnosticCode::InvalidArgument,
                                                    "invalid production work scaling input");
    const auto whole = effortNanoseconds / 1000;
    const auto tail = effortNanoseconds % 1000;
    if (whole > std::numeric_limits<std::int64_t>::max() / efficiencyPermille)
        return productionBindingFailure<ScaledWork>(eve::DiagnosticCode::InvalidArgument,
                                                    "scaled production work overflows duration");
    const auto tailNumerator = tail * static_cast<std::int64_t>(efficiencyPermille) + remainderPermille;
    const auto tailApplied = tailNumerator / 1000;
    const auto wholeApplied = whole * static_cast<std::int64_t>(efficiencyPermille);
    if (wholeApplied > std::numeric_limits<std::int64_t>::max() - tailApplied)
        return productionBindingFailure<ScaledWork>(eve::DiagnosticCode::InvalidArgument,
                                                    "scaled production work overflows duration");
    return eve::Result<ScaledWork>::success(
        {wholeApplied + tailApplied, static_cast<std::uint32_t>(tailNumerator % 1000)});
}

}  // namespace

WorkQueue::WorkQueue(eve::PersistentId instanceId) : instanceId_(instanceId) {}

std::string_view taskStateName(TaskState state) {
    switch (state) {
        case TaskState::Queued: return "queued";
        case TaskState::Running: return "running";
        case TaskState::Paused: return "paused";
        case TaskState::ReadyToSettle: return "ready_to_settle";
        case TaskState::SettlementFailed: return "settlement_failed";
        case TaskState::Completed: return "completed";
        case TaskState::Cancelled: return "cancelled";
        case TaskState::Failed: return "failed";
    }
    return "unknown";
}

std::string_view eventKindName(ProductionEventKind kind) {
    switch (kind) {
        case ProductionEventKind::Enqueued: return "enqueued";
        case ProductionEventKind::Started: return "started";
        case ProductionEventKind::Paused: return "paused";
        case ProductionEventKind::Resumed: return "resumed";
        case ProductionEventKind::ReadyToSettle: return "ready_to_settle";
        case ProductionEventKind::SettlementFailed: return "settlement_failed";
        case ProductionEventKind::Completed: return "completed";
        case ProductionEventKind::Cancelled: return "cancelled";
        case ProductionEventKind::Failed: return "failed";
    }
    return "unknown";
}

void WorkQueue::emit(ProductionEventKind kind, const ProductionTask& task, std::string_view reason) {
    events_.push_back(
        {{nextEventSequence_++, std::string(reason)}, tick_, kind, task.id, task.owner, task.kind, task.product,
         task.correlationId});
    if (const auto next = revision_.incremented()) revision_ = *next;
}

int WorkQueue::slotCount(std::string_view owner) const {
    const auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return it != slots_.end() && it->first == owner ? it->second : 1;
}

int WorkQueue::runningCount(std::string_view owner) const {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& task) {
        return task->owner == owner && task->state == TaskState::Running;
    }));
}

int WorkQueue::resourceCapacity(std::string_view owner, std::string_view resource) const {
    if (resource == "slot") return slotCount(owner);
    const auto key = std::pair(std::string(owner), std::string(resource));
    const auto it = std::lower_bound(resources_.begin(), resources_.end(), key,
                                     [](const auto& entry, const auto& wanted) { return entry.first < wanted; });
    return it != resources_.end() && it->first == key ? it->second : 0;
}

bool WorkQueue::resourcesAvailable(const ProductionTask& task) const {
    for (const auto& requirement : task.resources) {
        int used = 0;
        for (const auto& running : tasks_) {
            if (running->owner != task.owner || running->state != TaskState::Running) continue;
            for (const auto& held : running->resources)
                if (held.resource == requirement.resource) used += held.units;
        }
        if (used + requirement.units > resourceCapacity(task.owner, requirement.resource)) return false;
    }
    return true;
}

eve::Result<std::string> WorkQueue::blockReason(std::string_view taskId) const {
    const auto taskRef = find(taskId);
    if (!taskRef)
        return productionBindingFailure<std::string>(eve::DiagnosticCode::NotFound,
                                                     "work task was not found", "taskId");
    const auto& task = taskRef->get();
    if (!task.dependencies.prerequisites.empty()) {
        bool anyCompleted = false;
        for (const auto& dependencyId : task.dependencies.prerequisites) {
            const auto dependency = find(dependencyId);
            if (!dependency)
                return eve::Result<std::string>::success("missing_prerequisite:" + dependencyId);
            if (dependency->get().state == TaskState::Completed) {
                anyCompleted = true;
                continue;
            }
            if (task.dependencies.mode == TaskDependencyMode::AllOf)
                return eve::Result<std::string>::success(
                    std::string(terminal(dependency->get().state) ? "prerequisite_failed:" : "waiting_for:") +
                    dependencyId);
        }
        if (task.dependencies.mode == TaskDependencyMode::AnyOf && !anyCompleted)
            return eve::Result<std::string>::success("waiting_for_any_prerequisite");
    }
    for (const auto& definition : task.dependencies.requiredDefinitions) {
        if (!definitionAvailable(task.owner, definition))
            return eve::Result<std::string>::success(
                "requires_definition:" + definition.reference.id().format() + "@" +
                std::to_string(definition.generation.value()));
    }
    for (const auto& tag : task.dependencies.requiredTags)
        if (!tagAvailable(task.owner, tag))
            return eve::Result<std::string>::success("requires_tag:" + tag);
    for (const auto& blockerId : task.dependencies.blocks) {
        const auto blocker = find(blockerId);
        if (!blocker)
            return eve::Result<std::string>::success("missing_blocker:" + blockerId);
        if (!terminal(blocker->get().state))
            return eve::Result<std::string>::success("blocked_by:" + blockerId);
    }
    if (task.repeat.maintainStockTarget >= 0 && task.repeat.observedStock >= task.repeat.maintainStockTarget)
        return eve::Result<std::string>::success("stock_target_satisfied");
    if (!resourcesAvailable(task)) return eve::Result<std::string>::success("resource_unavailable");
    return eve::Result<std::string>::success(std::string{});
}

bool WorkQueue::dependenciesSatisfied(const ProductionTask& task) const {
    auto result = blockReason(task.id);
    return result.ok() && result.value().empty();
}

void WorkQueue::scheduleAll() {
    std::set<std::string> owners;
    for (const auto& task : tasks_) owners.insert(task->owner);
    for (const auto& owner : owners) schedule(owner);
}

void WorkQueue::schedule(std::string_view owner) {
    int available = slotCount(owner) - runningCount(owner);
    const auto strategy = schedulerStrategy(owner);
    while (available-- > 0) {
        ProductionTask* best = nullptr;
        for (auto& candidate : tasks_) {
            if (candidate->owner != owner || candidate->state != TaskState::Queued ||
                !dependenciesSatisfied(*candidate))
                continue;
            if (!best) {
                best = candidate.get();
                continue;
            }
            const auto remaining = [](const ProductionTask& value) {
                return value.duration.nanoseconds() - value.progress.nanoseconds();
            };
            const bool preferred = strategy == SchedulerStrategy::Fifo
                ? candidate->enqueueSequence < best->enqueueSequence
                : strategy == SchedulerStrategy::ShortestRemaining
                ? remaining(*candidate) < remaining(*best) ||
                      (remaining(*candidate) == remaining(*best) &&
                       candidate->enqueueSequence < best->enqueueSequence)
                : candidate->priority > best->priority ||
                      (candidate->priority == best->priority && candidate->enqueueSequence < best->enqueueSequence);
            if (preferred) best = candidate.get();
        }
        if (!best) break;
        best->state = TaskState::Running;
        if (best->reservationState == ReservationState::Reserved)
            best->reservationState = ReservationState::Started;
        emit(ProductionEventKind::Started, *best);
    }
}

eve::Result<std::string> WorkQueue::enqueue(std::string_view owner, std::string_view kind, std::string_view product,
                                            eve::Value context, double duration, int priority) {
    auto converted = eve::Duration::fromSeconds(duration);
    if (!converted) return eve::Result<std::string>::failure(converted.status());
    ProductionRequest request;
    request.owner = std::string(owner);
    request.kind = std::string(kind);
    request.product = std::string(product);
    request.context = std::move(context);
    request.duration = std::move(converted).takeValue();
    request.priority = priority;
    request.settlementRequired = false;
    return enqueue(std::move(request));
}

eve::Result<std::string> WorkQueue::enqueue(ProductionRequest request) {
    const auto& owner = request.owner;
    const auto& kind = request.kind;
    const auto& product = request.product;
    if (owner.empty() || kind.empty() || product.empty())
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task owner, kind and product are required");
    if (!request.context.isObject() || !request.reservation.isObject())
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task context and reservation must be Value objects");
    if (request.duration.nanoseconds() <= 0)
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task duration must be positive", "duration");
    if (request.batchSize == 0 || request.efficiencyPermille == 0)
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "batch size and efficiency must be positive", "batchSize");
    if (request.repeat.totalCycles == 0 || request.repeat.maintainStockTarget < -1 ||
        request.repeat.observedStock < 0 ||
        (request.random.drawCount > 0 && request.random.stream.empty()))
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "invalid repeat, stock or random provenance metadata");
    std::set<std::string> resourceNames;
    for (const auto& requirement : request.resources) {
        if (requirement.resource.empty() || requirement.units <= 0 ||
            !resourceNames.insert(requirement.resource).second)
            return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                         "resource requirements must be named, positive and unique",
                                                         "resources");
    }
    std::set<std::string> dependencyIds;
    const auto validateDependencies = [this, &dependencyIds](const std::vector<std::string>& values,
                                                              std::string_view path) -> eve::Result<void> {
        for (const auto& id : values) {
            if (id.empty() || !dependencyIds.insert(id).second)
                return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                       "production dependency ids must be non-empty and unique",
                                                       std::string(path));
            if (!find(id))
                return productionBindingFailure<void>(eve::DiagnosticCode::NotFound,
                                                       "production dependency does not exist", std::string(path));
        }
        return eve::Result<void>::success();
    };
    auto prerequisites = validateDependencies(request.dependencies.prerequisites, "dependencies.prerequisites");
    if (!prerequisites) return eve::Result<std::string>::failure(prerequisites.status());
    auto blockers = validateDependencies(request.dependencies.blocks, "dependencies.blocks");
    if (!blockers) return eve::Result<std::string>::failure(blockers.status());
    std::set<std::pair<std::string, std::uint64_t>> definitionRequirements;
    for (const auto& definition : request.dependencies.requiredDefinitions) {
        const auto key = std::pair(definition.reference.id().format(), definition.generation.value());
        if (!definition.isValid() || !definitionRequirements.insert(key).second)
            return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                         "required definitions must be valid and unique",
                                                         "dependencies.requiredDefinitions");
    }
    std::set<std::string> tagRequirements;
    for (const auto& tag : request.dependencies.requiredTags) {
        if (tag.empty() || !tagRequirements.insert(tag).second)
            return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                         "required tags must be non-empty and unique",
                                                         "dependencies.requiredTags");
    }
    auto               task = std::make_unique<ProductionTask>();
    std::ostringstream id;
    id << "task-" << std::setw(16) << std::setfill('0') << nextTaskId_;
    task->id                 = id.str();
    task->owner              = std::string(owner);
    task->kind               = std::string(kind);
    task->product            = std::string(product);
    task->context            = std::move(request.context);
    task->duration           = request.duration;
    task->priority           = request.priority;
    task->definition         = std::move(request.definition);
    task->reservation        = std::move(request.reservation);
    if (const auto* reservation = task->reservation.getIf<eve::Value::Object>();
        reservation != nullptr && !reservation->empty())
        task->reservationState = ReservationState::Reserved;
    task->dependencies       = std::move(request.dependencies);
    task->resources          = std::move(request.resources);
    task->termination        = request.termination;
    task->random             = std::move(request.random);
    task->repeat             = request.repeat;
    task->correlationId      = request.correlationId.empty() ? task->id : std::move(request.correlationId);
    task->batchSize          = request.batchSize;
    task->efficiencyPermille = request.efficiencyPermille;
    task->settlementRequired = request.settlementRequired;
    task->enqueueSequence    = nextEnqueueSequence_++;
    const std::string result = task->id;
    tasks_.push_back(std::move(task));
    ++nextTaskId_;
    emit(ProductionEventKind::Enqueued, *tasks_.back());
    schedule(owner);
    return eve::Result<std::string>::success(result);
}

ProductionTask* WorkQueue::mutableFind(std::string_view taskId) {
    const auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&taskId](const auto& task) { return task->id == taskId; });
    return it == tasks_.end() ? nullptr : it->get();
}

eve::OptionalRef<const ProductionTask> WorkQueue::find(std::string_view taskId) {
    const auto* task = mutableFind(taskId);
    return task == nullptr ? eve::OptionalRef<const ProductionTask>{}
                           : eve::OptionalRef<const ProductionTask>(std::cref(*task));
}

eve::OptionalRef<const ProductionTask> WorkQueue::find(std::string_view taskId) const {
    const auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&taskId](const auto& task) { return task->id == taskId; });
    return it == tasks_.end() ? eve::OptionalRef<const ProductionTask>{}
                              : eve::OptionalRef<const ProductionTask>(std::cref(*it->get()));
}

eve::Result<void> WorkQueue::pause(std::string_view taskId) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->state != TaskState::Queued && task->state != TaskState::Running)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict,
                                              "only queued or running tasks can be paused", "taskId");
    task->state = TaskState::Paused;
    emit(ProductionEventKind::Paused, *task);
    schedule(task->owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::resume(std::string_view taskId) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->state != TaskState::Paused)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "only paused tasks can be resumed",
                                              "taskId");
    task->state = TaskState::Queued;
    emit(ProductionEventKind::Resumed, *task);
    schedule(task->owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::cancel(std::string_view taskId, std::string_view reason) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (terminal(task->state))
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "terminal work tasks cannot be cancelled",
                                              "taskId");
    task->refundPermille = refundFor(*task, task->termination.cancellation);
    task->state  = TaskState::Cancelled;
    task->reason = std::string(reason);
    emit(ProductionEventKind::Cancelled, *task, reason);
    scheduleAll();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::fail(std::string_view taskId, std::string_view reason) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (terminal(task->state))
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "terminal work tasks cannot fail",
                                              "taskId");
    task->refundPermille = refundFor(*task, task->termination.failure);
    task->state  = TaskState::Failed;
    task->reason = std::string(reason);
    emit(ProductionEventKind::Failed, *task, reason);
    scheduleAll();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::settle(std::string_view taskId, ProductionSettlementReceipt receipt) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if ((!task->settlement.settlementId.empty() && task->settlement.settlementId == receipt.settlementId) ||
        (!task->lastSettlementId.empty() && task->lastSettlementId == receipt.settlementId))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    if (task->state != TaskState::ReadyToSettle && task->state != TaskState::SettlementFailed)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "work task is not ready to settle", "taskId");
    if (receipt.settlementId.empty() || !receipt.payload.isObject())
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "settlement requires an id and object payload", "receipt");
    task->settlement = std::move(receipt);
    if (task->reservationState == ReservationState::Started)
        task->reservationState = ReservationState::Consumed;
    task->reason.clear();
    task->state = TaskState::Completed;
    emit(ProductionEventKind::Completed, *task);
    continueAfterCycle(*task);
    scheduleAll();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::failSettlement(std::string_view taskId, std::string_view reason) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->state != TaskState::ReadyToSettle)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "work task is not ready to settle", "taskId");
    if (reason.empty())
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "settlement failure needs a reason");
    task->state = TaskState::SettlementFailed;
    task->reason = std::string(reason);
    emit(ProductionEventKind::SettlementFailed, *task, reason);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::retrySettlement(std::string_view taskId) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->state != TaskState::SettlementFailed)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "work task has no failed settlement", "taskId");
    task->state = TaskState::ReadyToSettle;
    task->reason.clear();
    emit(ProductionEventKind::ReadyToSettle, *task);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::uint32_t WorkQueue::refundFor(const ProductionTask& task, RefundPolicy policy) {
    if (policy == RefundPolicy::None) return 0;
    if (policy == RefundPolicy::Full) return 1000;
    const auto duration = task.duration.nanoseconds();
    if (duration <= 0 || task.progress >= task.duration) return 0;
    const auto remaining = duration - task.progress.nanoseconds();
    std::int64_t residue = 0;
    std::uint32_t permille = 0;
    for (std::uint32_t index = 0; index < 1000; ++index) {
        if (residue >= duration - remaining) {
            residue -= duration - remaining;
            ++permille;
        } else {
            residue += remaining;
        }
    }
    return std::min<std::uint32_t>(permille, 1000);
}

void WorkQueue::completeIfReady(ProductionTask& task) {
    if (task.progress < task.duration) return;
    task.progress = task.duration;
    task.workRemainderPermille = 0;
    if (task.reservationState == ReservationState::Started)
        task.reservationState = ReservationState::Consumed;
    if (task.settlementRequired) {
        task.state = TaskState::ReadyToSettle;
        emit(ProductionEventKind::ReadyToSettle, task);
    } else {
        task.settlement.settlementId = "automatic:" + task.id;
        task.state = TaskState::Completed;
        emit(ProductionEventKind::Completed, task);
        continueAfterCycle(task);
    }
}

eve::Result<void> WorkQueue::contribute(std::string_view taskId, WorkContribution contribution) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->state != TaskState::Running)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict,
                                              "work contributions require a running task", "taskId");
    if (contribution.contributor.empty() || contribution.effort.nanoseconds() <= 0 ||
        contribution.efficiencyPermille == 0)
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "work contribution must have a contributor, effort and efficiency");
    auto scaled = scaleWork(contribution.effort.nanoseconds(), contribution.efficiencyPermille,
                            task->workRemainderPermille);
    if (!scaled) return eve::Result<void>::failure(scaled.status());
    const auto remaining = task->duration.nanoseconds() - task->progress.nanoseconds();
    task->workRemainderPermille = scaled.value().remainderPermille;
    task->progress = eve::Duration::fromNanoseconds(
        task->progress.nanoseconds() + std::min(scaled.value().appliedNanoseconds, remaining));
    completeIfReady(*task);
    if (task->state != TaskState::Running) scheduleAll();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::advance(const eve::SimulationStep& step) {
    if (step.tick <= tick_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "production step tick must be strictly newer"));
    if (step.delta.nanoseconds() < 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "production step duration must be non-negative"));

    struct PendingProgress {
        ProductionTask* task = nullptr;
        std::int64_t appliedNanoseconds = 0;
        std::uint32_t remainderPermille = 0;
    };
    std::vector<PendingProgress> pending;
    pending.reserve(tasks_.size());
    const std::int64_t delta = step.delta.nanoseconds();
    for (auto& task : tasks_) {
        if (task->state != TaskState::Running) continue;
        auto scaled = scaleWork(delta, task->efficiencyPermille, task->workRemainderPermille);
        if (!scaled) return eve::Result<void>::failure(scaled.status());
        const auto remaining = task->duration.nanoseconds() - task->progress.nanoseconds();
        pending.push_back({task.get(), std::min(scaled.value().appliedNanoseconds, remaining),
                           scaled.value().remainderPermille});
    }

    tick_ = step.tick;
    bool completedAny = false;
    for (const auto& update : pending) {
        update.task->workRemainderPermille = update.remainderPermille;
        update.task->progress = eve::Duration::fromNanoseconds(
            update.task->progress.nanoseconds() + update.appliedNanoseconds);
        if (update.task->progress >= update.task->duration) {
            completeIfReady(*update.task);
            completedAny = true;
        }
    }
    if (completedAny) scheduleAll();
    return eve::Result<void>::success();
}

eve::Result<void> WorkQueue::setSlotCount(std::string_view owner, int slots) {
    if (owner.empty() || slots < 0)
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "owner must be non-empty and slots must be non-negative");
    auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                               [](const auto& entry, std::string_view key) { return entry.first < key; });
    if (it != slots_.end() && it->first == owner)
        it->second = slots;
    else
        slots_.insert(it, {std::string(owner), slots});
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::setResourceCapacity(std::string_view owner, std::string_view resource, int capacity) {
    if (owner.empty() || resource.empty() || resource == "slot" || capacity < 0)
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "resource capacity needs owner, non-slot name and non-negative value");
    const auto key = std::pair(std::string(owner), std::string(resource));
    auto it = std::lower_bound(resources_.begin(), resources_.end(), key,
                               [](const auto& entry, const auto& wanted) { return entry.first < wanted; });
    if (it != resources_.end() && it->first == key)
        it->second = capacity;
    else
        resources_.insert(it, {key, capacity});
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

int WorkQueue::taskCount() const { return static_cast<int>(tasks_.size()); }

eve::OptionalRef<const ProductionTask> WorkQueue::taskAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= tasks_.size()) return {};
    return std::ref(*tasks_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const ProductionTask> WorkQueue::taskAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= tasks_.size()) return {};
    return std::cref(*tasks_[static_cast<size_t>(index)]);
}

int WorkQueue::ownerTaskCount(std::string_view owner) const {
    return static_cast<int>(
        std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& t) { return t->owner == owner; }));
}

eve::OptionalRef<const ProductionTask> WorkQueue::ownerTaskAt(std::string_view owner, int index) {
    if (index < 0) return {};
    for (auto& task : tasks_)
        if (task->owner == owner && index-- == 0) return std::ref(*task);
    return {};
}

eve::OptionalRef<const ProductionTask> WorkQueue::ownerTaskAt(std::string_view owner, int index) const {
    if (index < 0) return {};
    for (const auto& task : tasks_)
        if (task->owner == owner && index-- == 0) return std::cref(*task);
    return {};
}

int WorkQueue::eventCount() const { return static_cast<int>(events_.size()); }

eve::OptionalRef<ProductionEvent> WorkQueue::eventAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::ref(events_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const ProductionEvent> WorkQueue::eventAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::cref(events_[static_cast<size_t>(index)]);
}

void WorkQueue::clearEvents() { events_.clear(); }

}  // namespace eve::production
