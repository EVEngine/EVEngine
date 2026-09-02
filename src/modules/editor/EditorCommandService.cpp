#include "editor/EditorCommandService.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace eve::editor {

namespace {

constexpr size_t kMaxPayloadDepth    = 32;
constexpr size_t kMaxPayloadElements = 100'000;

}  // namespace

EditorResult<EditorValue> EditorCommandService::error(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<EditorValue>(status, RuleId(rule), std::move(message));
}

EditorResult<void> EditorCommandService::checkExecutionPolicy(const CommandDescriptor& descriptor,
                                                             const CommandId&          id, CommandSource source,
                                                             const EditorValue& payload,
                                                             const HostProfile& profile) const {
    if (!profile.allowsCommand(id))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.profile-denied"),
                                         "Command is unavailable in this host: " + id.value());
    if (!profile.hasFeatures(descriptor.requiredFeatures))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.feature-denied"),
                                         "Command requires features unavailable in this host: " + id.value());
    if (source == CommandSource::Automation && !descriptor.automationAllowed)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.automation-denied"),
                                         "Command is not available to automation: " + id.value());
    if (!payload.isWithinLimits(kMaxPayloadDepth, kMaxPayloadElements, profile.maxPayloadBytes()))
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.payload-limit"),
                                         "Command payload exceeds host limits: " + id.value());
    return eve::editing::applied<void>();
}

template <class Output>
EditorResult<Output> denyPolicy(EditorResult<void>&& policy) {
    return EditorResult<Output>::failure(policy.status());
}

EditorResult<EditorValue> EditorCommandService::registerCommand(CommandDescriptor    descriptor,
                                                                EditorCommandHandler handler, bool replace) {
    if (!descriptor.id || descriptor.ownerModule.empty() || !handler)
        return error(EditorStatus::Rejected, "editor.command.invalid-registration",
                     "Command id, owner module and handler are required");

    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == descriptor.id; });
    if (it != commands_.end()) {
        if (!replace || it->descriptor.ownerModule != descriptor.ownerModule)
            return error(EditorStatus::Rejected, "editor.command.duplicate",
                         "Command is already registered: " + descriptor.id.value());
        *it = {std::move(descriptor), std::move(handler), {}, {}, nextRegistrationGeneration_++};
    } else {
        commands_.push_back({std::move(descriptor), std::move(handler), {}, {}, nextRegistrationGeneration_++});
    }
    ++revision_;
    return eve::editing::applied<EditorValue>(EditorValue{});
}

EditorResult<EditorValue> EditorCommandService::registerPlannedCommand(CommandDescriptor         descriptor,
                                                                       EditorCommandPlanner      planner,
                                                                       EditorCommandPlanExecutor executor,
                                                                       bool                      replace) {
    if (!descriptor.id || descriptor.ownerModule.empty() || !planner || !executor)
        return error(EditorStatus::Rejected, "editor.command.invalid-registration",
                     "Command id, owner module, planner and executor are required");
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == descriptor.id; });
    if (it != commands_.end()) {
        if (!replace || it->descriptor.ownerModule != descriptor.ownerModule)
            return error(EditorStatus::Rejected, "editor.command.duplicate",
                         "Command is already registered: " + descriptor.id.value());
        *it = {std::move(descriptor), {}, std::move(planner), std::move(executor),
               nextRegistrationGeneration_++};
    } else {
        commands_.push_back({std::move(descriptor), {}, std::move(planner), std::move(executor),
                             nextRegistrationGeneration_++});
    }
    ++revision_;
    return eve::editing::applied<EditorValue>(EditorValue{});
}

bool EditorCommandService::unregisterCommand(const CommandId& id, const std::string& ownerModule) {
    auto it = std::find_if(commands_.begin(), commands_.end(), [&](const Registration& entry) {
        return entry.descriptor.id == id && (ownerModule.empty() || entry.descriptor.ownerModule == ownerModule);
    });
    if (it == commands_.end()) return false;
    commands_.erase(it);
    ++revision_;
    return true;
}

size_t EditorCommandService::unregisterOwner(const std::string& ownerModule) {
    if (ownerModule.empty()) return 0;
    const size_t before = commands_.size();
    std::erase_if(commands_, [&](const Registration& entry) { return entry.descriptor.ownerModule == ownerModule; });
    const size_t removed = before - commands_.size();
    if (removed > 0) ++revision_;
    return removed;
}

void EditorCommandService::clear() {
    if (commands_.empty()) return;
    commands_.clear();
    ++revision_;
}

const CommandDescriptor* EditorCommandService::find(const CommandId& id) const {
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == id; });
    return it == commands_.end() ? nullptr : &it->descriptor;
}

bool EditorCommandService::supportsPlanning(const CommandId& id) const {
    const auto it = std::find_if(commands_.begin(), commands_.end(),
                                 [&](const Registration& entry) { return entry.descriptor.id == id; });
    return it != commands_.end() && static_cast<bool>(it->planner) && static_cast<bool>(it->planExecutor);
}

std::vector<CommandDescriptor> EditorCommandService::commands(const HostProfile& profile) const {
    std::vector<CommandDescriptor> result;
    result.reserve(commands_.size());
    for (const auto& entry : commands_)
        if (profile.allowsCommand(entry.descriptor.id) && profile.hasFeatures(entry.descriptor.requiredFeatures))
            result.push_back(entry.descriptor);
    return result;
}

EditorResult<EditorValue> EditorCommandService::execute(const CommandId& id, const CommandContext& context,
                                                        const EditorValue& payload) const {
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == id; });
    if (it == commands_.end())
        return error(EditorStatus::NotFound, "editor.command.not-found", "Command is not registered: " + id.value());
    if (!context.profile)
        return error(EditorStatus::Failed, "editor.command.missing-profile", "Command context has no host profile");
    if (auto policy = checkExecutionPolicy(it->descriptor, id, context.source, payload, *context.profile);
        !policy.ok())
        return denyPolicy<EditorValue>(std::move(policy));

    if (!it->handler)
        return error(EditorStatus::Unsupported, "editor.command.requires-plan",
                     "Command must be planned before it can be executed: " + id.value());
    try {
        return it->handler(context, payload);
    } catch (const std::exception& exception) {
        return error(EditorStatus::Failed, "editor.command.handler-exception",
                     "Command handler failed: " + std::string(exception.what()));
    } catch (...) {
        return error(EditorStatus::Failed, "editor.command.handler-exception",
                     "Command handler failed with an unknown exception");
    }
}

EditorResult<CommandPlan> EditorCommandService::plan(const CommandRequest& request, const HostProfile& profile) const {
    auto failure = [](EditorStatus status, const char* rule, std::string message) {
        return eve::editing::failed<CommandPlan>(status, RuleId(rule), std::move(message));
    };
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == request.id; });
    if (it == commands_.end())
        return failure(EditorStatus::NotFound, "editor.command.not-found",
                       "Command is not registered: " + request.id.value());
    if (auto policy = checkExecutionPolicy(it->descriptor, request.id, request.source, request.payload, profile);
        !policy.ok())
        return denyPolicy<CommandPlan>(std::move(policy));
    if (request.expectedRevision && *request.expectedRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.revision-conflict",
                       "Expected revision does not match the captured context");
    if (!it->planner)
        return failure(EditorStatus::Unsupported, "editor.command.planning-unsupported",
                       "Command does not provide a planning handler");
    try {
        EditorResult<CommandPlan> result = it->planner(request);
        if (result.ok()) {
            result.value().command      = request.id;
            result.value().target       = request.context.target;
            result.value().baseRevision = request.context.targetRevision;
            if (result.value().targetGeneration == 0)
                result.value().targetGeneration = request.context.targetGeneration;
            result.value().commandGeneration = it->generation;
            result.value().plannedPayload     = request.payload;
            result.value().plannedSource      = request.source;
            if (result.value().id.empty())
                result.value().id = PlanId(request.id.value() + ".plan." + std::to_string(++planSequence_));
        }
        return result;
    } catch (const std::exception& exception) {
        return failure(EditorStatus::Failed, "editor.command.planner-exception", exception.what());
    } catch (...) {
        return failure(EditorStatus::Failed, "editor.command.planner-exception",
                       "Command planner failed with an unknown exception");
    }
}

EditorResult<TransactionReceipt> EditorCommandService::executePlan(const CommandRequest& request,
                                                                   const CommandPlan&    plan,
                                                                   const HostProfile&    profile) const {
    auto failure = [](EditorStatus status, const char* rule, std::string message) {
        return eve::editing::failed<TransactionReceipt>(status, RuleId(rule), std::move(message));
    };
    if (plan.id.empty() || plan.command != request.id || plan.target != request.context.target)
        return failure(EditorStatus::Rejected, "editor.command.invalid-plan",
                       "Plan identity or target does not match the request");
    if (plan.baseRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.stale-plan",
                       "Plan was produced for another target revision");
    if (plan.targetGeneration != request.context.targetGeneration)
        return failure(EditorStatus::Conflict, "editor.command.stale-target-generation",
                       "Plan was produced for another target lifetime");
    if (request.expectedRevision && *request.expectedRevision != request.context.targetRevision)
        return failure(EditorStatus::Conflict, "editor.command.revision-conflict",
                       "Expected revision does not match the captured context");
    auto it = std::find_if(commands_.begin(), commands_.end(),
                           [&](const Registration& entry) { return entry.descriptor.id == request.id; });
    if (it == commands_.end())
        return failure(EditorStatus::NotFound, "editor.command.not-found",
                       "Command is not registered: " + request.id.value());
    if (plan.commandGeneration != it->generation)
        return failure(EditorStatus::Conflict, "editor.command.stale-registration",
                       "Command registration changed after planning");
    if (auto policy = checkExecutionPolicy(it->descriptor, request.id, request.source, request.payload, profile);
        !policy.ok())
        return denyPolicy<TransactionReceipt>(std::move(policy));
    if (request.payload != plan.plannedPayload || request.source != plan.plannedSource)
        return failure(EditorStatus::Rejected, "editor.command.plan-input-mismatch",
                       "Plan payload or source differs from the validated request");
    if (!it->planExecutor)
        return failure(EditorStatus::Unsupported, "editor.command.execution-unsupported",
                       "Command does not provide a plan executor");
    try {
        return it->planExecutor(request, plan);
    } catch (const std::exception& exception) {
        return failure(EditorStatus::Failed, "editor.command.executor-exception", exception.what());
    } catch (...) {
        return failure(EditorStatus::Failed, "editor.command.executor-exception",
                       "Command executor failed with an unknown exception");
    }
}

}  // namespace eve::editor
