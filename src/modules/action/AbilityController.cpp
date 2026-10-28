#include "action/AbilityController.h"

#include "common/Diagnostic.h"

#include <utility>

namespace eve::action {

Result<void> PlayerAbilityIntentQueue::enqueue(AbilityIntent intent) {
    if (intent.grantId.isZero())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Ability intent grant id must be non-zero", "grantId"));
    intents_.push_back(std::move(intent));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::optional<AbilityIntent>> PlayerAbilityIntentQueue::nextIntent(SimulationTick) {
    if (intents_.empty())
        return Result<std::optional<AbilityIntent>>::success(std::nullopt, Status::success(StatusCode::NoOp));
    AbilityIntent intent = std::move(intents_.front());
    intents_.pop_front();
    return Result<std::optional<AbilityIntent>>::success(std::move(intent));
}

Result<std::optional<AbilityActivation>> AbilityControllerRuntime::processNext(IAbilityIntentSource& source,
                                                                                SimulationTick         tick) {
    auto next = source.nextIntent(tick);
    if (!next) return Result<std::optional<AbilityActivation>>::failure(next.status());
    if (!next.value())
        return Result<std::optional<AbilityActivation>>::success(std::nullopt, Status::success(StatusCode::NoOp));

    AbilityIntent intent       = std::move(*next.value());
    intent.request.requestedTick = tick;
    auto activation = abilities_.activate(intent.grantId, std::move(intent.request), tick);
    if (!activation) return Result<std::optional<AbilityActivation>>::failure(activation.status());
    return Result<std::optional<AbilityActivation>>::success(std::move(activation).takeValue(),
                                                              Status::success(StatusCode::Applied));
}

}  // namespace eve::action
