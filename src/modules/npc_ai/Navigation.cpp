#include "npc_ai/Navigation.h"

#include <algorithm>
#include <cmath>

namespace eve::npc_ai {
namespace {
template <class T>
Result<T> navigationFailure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "npc_ai.navigation"));
}

bool finitePosition(const std::array<double, 3>& position) {
    return std::all_of(position.begin(), position.end(), [](double value) { return std::isfinite(value); });
}
}  // namespace

NavigationTaskService::NavigationTaskService(std::unique_ptr<INavigationProvider>       provider,
                                             std::unique_ptr<INavigationRequestFactory> requestFactory)
    : provider_(std::move(provider)), requestFactory_(std::move(requestFactory)) {}

Result<std::unique_ptr<NavigationTaskService>> NavigationTaskService::create(
    std::unique_ptr<INavigationProvider> provider, std::unique_ptr<INavigationRequestFactory> requestFactory) {
    if (!provider || !requestFactory)
        return navigationFailure<std::unique_ptr<NavigationTaskService>>(
            DiagnosticCode::InvalidArgument, "navigation provider and request factory owners are required");
    return Result<std::unique_ptr<NavigationTaskService>>::success(std::unique_ptr<NavigationTaskService>(
        new NavigationTaskService(std::move(provider), std::move(requestFactory))));
}

NavigationTaskService::~NavigationTaskService() {
    for (const auto& [task, ticket] : active_) {
        (void)task;
        provider_->abandon(ticket);
    }
}

NavigationTaskService::TaskKey NavigationTaskService::key(const TaskContext& context, const TaskSpec& spec) {
    return {context.agent, std::string(context.stateId), spec.id};
}

Result<void> NavigationTaskService::start(const TaskContext& context, const TaskSpec& spec,
                                          std::string& inOutMemoryJson) {
    const auto taskKey = key(context, spec);
    if (active_.contains(taskKey))
        return navigationFailure<void>(DiagnosticCode::AlreadyExists, "navigation task already owns an active ticket");
    auto request = requestFactory_->create(context, spec);
    if (!request.ok()) return Result<void>::failure(request.status());
    if (request.value().agent != context.agent || !finitePosition(request.value().start) ||
        !finitePosition(request.value().destination) || !std::isfinite(request.value().agentRadius) ||
        !std::isfinite(request.value().acceptanceRadius) || request.value().agentRadius <= 0.0 ||
        request.value().acceptanceRadius < 0.0 || request.value().requestedTick != context.simulationTick)
        return navigationFailure<void>(DiagnosticCode::InvalidArgument,
                                       "navigation request factory returned an invalid request");
    auto ticket = provider_->begin(request.value());
    if (!ticket.ok()) return Result<void>::failure(ticket.status());
    if (!ticket.value().isValid()) {
        return navigationFailure<void>(DiagnosticCode::InvariantViolation,
                                       "navigation provider returned an invalid ticket");
    }
    active_.emplace(taskKey, ticket.value());
    inOutMemoryJson = "{\"phase\":\"pending\"}";
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<TaskStatus> NavigationTaskService::tick(const TaskContext& context, const TaskSpec& spec,
                                               std::string& inOutMemoryJson) {
    const auto found = active_.find(key(context, spec));
    if (found == active_.end())
        return navigationFailure<TaskStatus>(DiagnosticCode::PreconditionViolation,
                                             "navigation task has no active ticket");
    auto progress = provider_->poll(found->second, context.simulationTick);
    if (!progress.ok()) return Result<TaskStatus>::failure(progress.status());
    if (!finitePosition(progress.value().desiredVelocity) || !std::isfinite(progress.value().remainingDistance) ||
        progress.value().remainingDistance < 0.0)
        return navigationFailure<TaskStatus>(DiagnosticCode::InvariantViolation,
                                             "navigation provider returned invalid progress");
    switch (progress.value().phase) {
        case NavigationPhase::Pending:
            inOutMemoryJson = "{\"phase\":\"pending\"}";
            return Result<TaskStatus>::success(TaskStatus::Running);
        case NavigationPhase::Moving:
            inOutMemoryJson = "{\"phase\":\"moving\"}";
            return Result<TaskStatus>::success(TaskStatus::Running);
        case NavigationPhase::Arrived:
            active_.erase(found);
            inOutMemoryJson = "{\"phase\":\"arrived\"}";
            return Result<TaskStatus>::success(TaskStatus::Succeeded);
        case NavigationPhase::Unreachable:
            active_.erase(found);
            inOutMemoryJson = "{\"phase\":\"unreachable\"}";
            return Result<TaskStatus>::success(TaskStatus::Failed);
        case NavigationPhase::Cancelled:
            active_.erase(found);
            inOutMemoryJson = "{\"phase\":\"cancelled\"}";
            return Result<TaskStatus>::success(TaskStatus::Failed);
    }
    return navigationFailure<TaskStatus>(DiagnosticCode::InvariantViolation,
                                         "navigation provider returned an unknown phase");
}

void NavigationTaskService::stop(const TaskContext& context, const TaskSpec& spec, StopReason reason,
                                 std::string_view memoryJson) noexcept {
    (void)reason;
    (void)memoryJson;
    const auto found = active_.find(key(context, spec));
    if (found == active_.end()) return;
    provider_->abandon(found->second);
    active_.erase(found);
}

}  // namespace eve::npc_ai
