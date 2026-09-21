#include "production/Production.h"

#include <algorithm>
#include <limits>

namespace eve::production {
namespace {

template <class T>
eve::Result<T> advancedFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "production.advanced"));
}

eve::Result<std::int64_t> scaledRemaining(const ProductionTask& task) {
    const auto remaining = task.duration.nanoseconds() - task.progress.nanoseconds();
    const auto efficiency = static_cast<std::int64_t>(task.efficiencyPermille);
    if (remaining <= 0) return eve::Result<std::int64_t>::success(0);
    const auto whole = remaining / efficiency;
    if (whole > std::numeric_limits<std::int64_t>::max() / 1000)
        return advancedFailure<std::int64_t>(eve::DiagnosticCode::InvalidArgument,
                                             "production remaining time is not representable");
    const auto residualNumerator = (remaining % efficiency) * 1000 - task.workRemainderPermille;
    const auto residual = residualNumerator <= 0 ? 0 : (residualNumerator + efficiency - 1) / efficiency;
    const auto base = whole * 1000;
    if (base > std::numeric_limits<std::int64_t>::max() - residual)
        return advancedFailure<std::int64_t>(eve::DiagnosticCode::InvalidArgument,
                                             "production remaining time is not representable");
    return eve::Result<std::int64_t>::success(base + residual);
}

}  // namespace

SchedulerStrategy WorkQueue::schedulerStrategy(std::string_view owner) const {
    const auto it = std::lower_bound(schedulerStrategies_.begin(), schedulerStrategies_.end(), owner,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return it != schedulerStrategies_.end() && it->first == owner ? it->second : SchedulerStrategy::Priority;
}

bool WorkQueue::definitionAvailable(
    std::string_view owner, const eve::definition::DefinitionHandle& definition) const {
    const auto key = std::tuple(std::string(owner), definition.reference.id().format(), definition.generation.value());
    return std::binary_search(availableDefinitions_.begin(), availableDefinitions_.end(), key);
}

bool WorkQueue::tagAvailable(std::string_view owner, std::string_view tag) const {
    return std::binary_search(availableTags_.begin(), availableTags_.end(),
                              std::pair(std::string(owner), std::string(tag)));
}

eve::Result<void> WorkQueue::setDefinitionAvailable(
    std::string_view owner, const eve::definition::DefinitionHandle& definition, bool available) {
    if (owner.empty() || !definition.isValid())
        return advancedFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "definition availability needs an owner and valid handle");
    const auto key = std::tuple(std::string(owner), definition.reference.id().format(), definition.generation.value());
    auto it = std::lower_bound(availableDefinitions_.begin(), availableDefinitions_.end(), key);
    const bool exists = it != availableDefinitions_.end() && *it == key;
    if (available && !exists) availableDefinitions_.insert(it, key);
    if (!available && exists) availableDefinitions_.erase(it);
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(exists == available
        ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::setTagAvailable(std::string_view owner, std::string_view tag, bool available) {
    if (owner.empty() || tag.empty())
        return advancedFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "tag availability needs an owner and tag");
    const auto key = std::pair(std::string(owner), std::string(tag));
    auto it = std::lower_bound(availableTags_.begin(), availableTags_.end(), key);
    const bool exists = it != availableTags_.end() && *it == key;
    if (available && !exists) availableTags_.insert(it, key);
    if (!available && exists) availableTags_.erase(it);
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(exists == available
        ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::setSchedulerStrategy(std::string_view owner, SchedulerStrategy strategy) {
    if (owner.empty())
        return advancedFailure<void>(eve::DiagnosticCode::InvalidArgument, "scheduler owner is required", "owner");
    auto it = std::lower_bound(schedulerStrategies_.begin(), schedulerStrategies_.end(), owner,
                               [](const auto& entry, std::string_view key) { return entry.first < key; });
    if (it != schedulerStrategies_.end() && it->first == owner)
        it->second = strategy;
    else
        schedulerStrategies_.insert(it, {std::string(owner), strategy});
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void WorkQueue::continueAfterCycle(ProductionTask& task) {
    ++task.completedCycles;
    const bool repeats = task.repeat.continuous || task.completedCycles < task.repeat.totalCycles;
    if (!repeats) return;
    if (task.repeat.maintainStockTarget >= 0) {
        const auto updated = static_cast<std::int64_t>(task.repeat.observedStock) + task.batchSize;
        task.repeat.observedStock = static_cast<int>(std::min<std::int64_t>(updated, std::numeric_limits<int>::max()));
    }
    task.progress = eve::Duration::zero();
    task.workRemainderPermille = 0;
    task.reason.clear();
    task.lastSettlementId = task.settlement.settlementId;
    task.settlement = {};
    if (task.reservationState == ReservationState::Consumed)
        task.reservationState = ReservationState::Reserved;
    task.state = TaskState::Queued;
}

eve::Result<ProductionReservationRelease> WorkQueue::releaseReservation(
    std::string_view taskId, std::string_view releaseId) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return advancedFailure<ProductionReservationRelease>(eve::DiagnosticCode::NotFound,
                                                             "work task was not found", "taskId");
    if (releaseId.empty())
        return advancedFailure<ProductionReservationRelease>(eve::DiagnosticCode::InvalidArgument,
                                                             "reservation release id is required", "releaseId");
    if (task->reservationState == ReservationState::Released &&
        task->reservationRelease.releaseId == releaseId)
        return eve::Result<ProductionReservationRelease>::success(
            task->reservationRelease, eve::Status::success(eve::StatusCode::NoOp));
    if (task->state != TaskState::Cancelled && task->state != TaskState::Failed)
        return advancedFailure<ProductionReservationRelease>(eve::DiagnosticCode::Conflict,
                                                             "only cancelled or failed tasks can release reservations",
                                                             "taskId");
    if (task->reservationState == ReservationState::None || task->reservationState == ReservationState::Released)
        return advancedFailure<ProductionReservationRelease>(eve::DiagnosticCode::Conflict,
                                                             "task has no releasable reservation", "taskId");
    task->reservationRelease = {std::string(releaseId), task->reservation, task->refundPermille};
    task->reservationState = ReservationState::Released;
    return eve::Result<ProductionReservationRelease>::success(
        task->reservationRelease, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::reportStock(std::string_view taskId, int observedStock) {
    auto* task = mutableFind(taskId);
    if (task == nullptr)
        return advancedFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    if (task->repeat.maintainStockTarget < 0 || observedStock < 0)
        return advancedFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "task has no maintain-stock policy or stock is negative", "observedStock");
    task->repeat.observedStock = observedStock;
    schedule(task->owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::uint32_t> WorkQueue::advanceTo(eve::SimulationTick target, eve::Duration fixedDelta,
                                                std::uint32_t maxSteps) {
    if (target < tick_ || fixedDelta.nanoseconds() <= 0 || maxSteps == 0)
        return advancedFailure<std::uint32_t>(eve::DiagnosticCode::InvalidArgument,
                                              "advanceTo needs a future target, positive delta and step budget");
    const auto distance = target.value() - tick_.value();
    if (distance > maxSteps)
        return advancedFailure<std::uint32_t>(eve::DiagnosticCode::PreconditionViolation,
                                              "advanceTo step budget is insufficient", "maxSteps");
    std::uint32_t applied = 0;
    while (tick_ < target) {
        auto step = advance({eve::SimulationTick(tick_.value() + 1), fixedDelta});
        if (!step) return eve::Result<std::uint32_t>::failure(step.status());
        ++applied;
    }
    return eve::Result<std::uint32_t>::success(applied);
}

eve::Result<ProductionPrediction> WorkQueue::predict(std::string_view taskId) const {
    const auto reference = find(taskId);
    if (!reference)
        return advancedFailure<ProductionPrediction>(eve::DiagnosticCode::NotFound,
                                                     "work task was not found", "taskId");
    const auto& task = reference->get();
    ProductionPrediction prediction;
    auto reason = blockReason(taskId);
    if (!reason) return eve::Result<ProductionPrediction>::failure(reason.status());
    prediction.blockReason = reason.value();
    auto own = scaledRemaining(task);
    if (!own) return eve::Result<ProductionPrediction>::failure(own.status());
    prediction.ownWorkRemaining = eve::Duration::fromNanoseconds(own.value());
    if (task.state == TaskState::Running) {
        prediction.estimatedCompletionAfter = prediction.ownWorkRemaining;
        prediction.exact = true;
        return eve::Result<ProductionPrediction>::success(std::move(prediction));
    }
    std::int64_t earliest = 0;
    if (runningCount(task.owner) >= slotCount(task.owner)) {
        earliest = std::numeric_limits<std::int64_t>::max();
        for (const auto& candidate : tasks_)
            if (candidate->owner == task.owner && candidate->state == TaskState::Running)
                {
                    auto candidateRemaining = scaledRemaining(*candidate);
                    if (!candidateRemaining)
                        return eve::Result<ProductionPrediction>::failure(candidateRemaining.status());
                    earliest = std::min(earliest, candidateRemaining.value());
                }
        if (earliest == std::numeric_limits<std::int64_t>::max()) earliest = 0;
    }
    prediction.estimatedStartAfter = eve::Duration::fromNanoseconds(earliest);
    if (earliest > std::numeric_limits<std::int64_t>::max() - own.value())
        return advancedFailure<ProductionPrediction>(eve::DiagnosticCode::InvalidArgument,
                                                     "production completion estimate is not representable");
    prediction.estimatedCompletionAfter = eve::Duration::fromNanoseconds(earliest + own.value());
    prediction.exact = prediction.blockReason.empty() && task.state == TaskState::Queued && earliest == 0;
    return eve::Result<ProductionPrediction>::success(std::move(prediction));
}

eve::Result<ProductionDiagnostic> WorkQueue::diagnose(std::string_view taskId) const {
    const auto reference = find(taskId);
    if (!reference)
        return advancedFailure<ProductionDiagnostic>(eve::DiagnosticCode::NotFound,
                                                     "work task was not found", "taskId");
    const auto& task = reference->get();
    auto reason = blockReason(taskId);
    if (!reason) return eve::Result<ProductionDiagnostic>::failure(reason.status());
    ProductionDiagnostic diagnostic;
    diagnostic.correlationId = task.correlationId;
    diagnostic.taskId = task.id;
    diagnostic.tick = tick_;
    diagnostic.code = reason.value().empty() ? std::string(taskStateName(task.state)) : reason.value();
    diagnostic.message = reason.value().empty() ? "task state is " + std::string(taskStateName(task.state))
                                                : "task is blocked: " + reason.value();
    return eve::Result<ProductionDiagnostic>::success(std::move(diagnostic));
}

}  // namespace eve::production
