#include "rts/RTSAction.h"

#include "action/Action.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

bool terminal(action::ActionPhase phase) {
    return phase == action::ActionPhase::Completed || phase == action::ActionPhase::Cancelled ||
           phase == action::ActionPhase::Failed;
}

std::optional<LogicalId> actionIdFor(const OrderRecord& order) {
    if (!order.definitionId.empty()) return LogicalId::parse(order.definitionId);
    return LogicalId::fromParts("rts", "command." + std::string(orderKindName(order.kind)));
}

}  // namespace

struct ActionAdapter::Impl {
    struct Pending {
        ecs::EntityHandle         unit;
        std::string               orderId;
        action::ActionExecutionId execution;
    };

    explicit Impl(action::ActionRuntime& runtimeValue) : runtime(runtimeValue) {}

    action::ActionRuntime& runtime;
    std::vector<Pending>   pending;
};

ActionAdapter::ActionAdapter(action::ActionRuntime& runtime) : impl_(std::make_unique<Impl>(runtime)) {}

ActionAdapter::~ActionAdapter() = default;

void ActionAdapter::clear() noexcept {
    if (impl_) impl_->pending.clear();
}

Result<ActionExecutionResult> ActionAdapter::execute(Unit& unit, const OrderRecord& order, const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<ActionExecutionResult>(DiagnosticCode::InvalidArgument,
                                              "RTS action step delta must be non-negative", "step.delta");
    if (!impl_)
        return failure<ActionExecutionResult>(DiagnosticCode::InvariantViolation,
                                              "RTS action adapter is not initialized", "adapter");

    const ecs::EntityHandle unitHandle = ecs::handle_of(&unit);
    auto                    pendingForUnit =
        std::find_if(impl_->pending.begin(), impl_->pending.end(),
                     [&unitHandle](const Impl::Pending& pending) { return sameHandle(pending.unit, unitHandle); });

    if (pendingForUnit != impl_->pending.end() && pendingForUnit->orderId != order.id) {
        const auto* oldExecution = impl_->runtime.find(pendingForUnit->execution);
        if (oldExecution != nullptr && !terminal(oldExecution->phase())) {
            auto cancelled = impl_->runtime.cancel(pendingForUnit->execution, step.tick);
            if (!cancelled) cancelled.ignore("RTS order replacement could not cancel old action");
        }
        impl_->pending.erase(pendingForUnit);
        pendingForUnit = impl_->pending.end();
    }

    action::ActionExecutionId executionId;
    if (pendingForUnit == impl_->pending.end()) {
        const auto logicalId = actionIdFor(order);
        if (!logicalId)
            return failure<ActionExecutionResult>(
                DiagnosticCode::InvalidArgument, "RTS order definition is not a valid LogicalId", "order.definitionId");

        action::ActionDefinition definition;
        definition.id = *logicalId;

        action::ActionRequest request;
        request.actionId      = definition.id;
        request.source        = unitHandle;
        request.requestedTick = step.tick;

        auto submitted = impl_->runtime.submit(std::move(definition), std::move(request));
        if (!submitted) return failureFrom<ActionExecutionResult>(submitted.status());
        executionId = std::move(submitted).takeValue();
        impl_->pending.push_back({unitHandle, order.id, executionId});
    } else {
        executionId = pendingForUnit->execution;
    }

    auto advanced = impl_->runtime.advance(executionId, step.tick, step.delta);
    if (!advanced) {
        impl_->pending.erase(
            std::remove_if(impl_->pending.begin(), impl_->pending.end(),
                           [&executionId](const Impl::Pending& pending) { return pending.execution == executionId; }),
            impl_->pending.end());
        return failureFrom<ActionExecutionResult>(advanced.status());
    }

    const auto summary   = std::move(advanced).takeValue();
    bool       completed = summary.phase == action::ActionPhase::Completed;
    if (order.kind == OrderKind::Move && !unit.motion()->arrived) completed = false;
    if (completed) {
        impl_->pending.erase(
            std::remove_if(impl_->pending.begin(), impl_->pending.end(),
                           [&executionId](const Impl::Pending& pending) { return pending.execution == executionId; }),
            impl_->pending.end());
    }

    const ActionExecutionResult result{completed ? ActionDisposition::Completed : ActionDisposition::Pending};
    return Result<ActionExecutionResult>::success(
        result, Status::success(completed ? StatusCode::Applied : StatusCode::Pending));
}

}  // namespace eve::rts
