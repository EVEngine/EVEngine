#include "editor/EditorAutomationProvider.h"

#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorValueJson.h"
#include "common/Capability.h"
#include "rx/Rx.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace eve::editor {

namespace {

const char* statusName(EditorStatus status) {
    switch (status) {
        case EditorStatus::Ok: return "ok";
        case EditorStatus::Applied: return "applied";
        case EditorStatus::Pending: return "pending";
        case EditorStatus::NoOp: return "no-op";
        case EditorStatus::Rejected: return "rejected";
        case EditorStatus::Conflict: return "conflict";
        case EditorStatus::NotFound: return "not-found";
        case EditorStatus::Unsupported: return "unsupported";
        case EditorStatus::Cancelled: return "cancelled";
        case EditorStatus::Failed: return "failed";
    }
    return "failed";
}

const char* transactionStateName(TransactionState state) {
    switch (state) {
        case TransactionState::Planning: return "planning";
        case TransactionState::Previewing: return "previewing";
        case TransactionState::PendingAuthority: return "pending-authority";
        case TransactionState::Committed: return "committed";
        case TransactionState::RolledBack: return "rolled-back";
        case TransactionState::Rejected: return "rejected";
        case TransactionState::Conflicted: return "conflicted";
        case TransactionState::Failed: return "failed";
    }
    return "failed";
}

EditorValue diagnosticsValue(const std::vector<EditorDiagnostic>& diagnostics) {
    EditorValue::Array values;
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        EditorValue::Object value;
        value["rule"]     = editing::diagnosticRule(diagnostic).value();
        value["severity"] = static_cast<std::int64_t>(diagnostic.severity());
        value["message"]  = diagnostic.message();
        values.emplace_back(std::move(value));
    }
    return EditorValue(std::move(values));
}

EditorValue resultValue(EditorStatus status, const std::vector<EditorDiagnostic>& diagnostics) {
    EditorValue::Object result;
    result["status"]      = statusName(status);
    result["accepted"]    = status == EditorStatus::Applied || status == EditorStatus::Pending ||
                             status == EditorStatus::NoOp;
    result["diagnostics"] = diagnosticsValue(diagnostics);
    return EditorValue(std::move(result));
}

EditorValue receiptValue(const EditorResult<TransactionReceipt>& result) {
    EditorValue response = resultValue(result.code(), result.diagnostics());
    if (result.ok()) {
        auto* object                = response.getIf<EditorValue::Object>();
        (*object)["transactionId"]  = result.value().id.value();
        (*object)["state"]          = transactionStateName(result.value().state);
        (*object)["beforeRevision"] = static_cast<std::int64_t>(result.value().beforeRevision);
        (*object)["afterRevision"]  = static_cast<std::int64_t>(result.value().afterRevision);
    }
    return response;
}

const EditorValue* findField(const EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    return found == request.end() ? nullptr : &found->second;
}

std::string stringField(const EditorValue::Object& request, const char* key) {
    const EditorValue* value = findField(request, key);
    const auto* text = value ? value->getIf<std::string>() : nullptr;
    return text ? *text : std::string{};
}

const std::int64_t* integerField(const EditorValue::Object& request, const char* key) {
    const EditorValue* value = findField(request, key);
    return value ? value->getIf<std::int64_t>() : nullptr;
}

EditorValue valueField(const EditorValue::Object& request, const char* key) {
    const EditorValue* value = findField(request, key);
    return value ? *value : EditorValue{};
}

std::string errorJson(EditorStatus status, const char* rule, std::string message) {
    return editorValueToJson(resultValue(
        status, {editing::ruleDiagnostic(editing::diagnosticCodeForStatus(status), RuleId(rule),
                                         DiagnosticSeverity::Error, std::move(message))}));
}

}  // namespace

struct EditorAutomationProvider::ObservationSession {
    EditorValue::Object                          descriptor;
    rx::Subject<std::string>                     source;
    std::unique_ptr<rx::Observable<std::string>> distinct;
    rx::Subscription                             subscription;
    std::vector<std::string>                     pending;
};

EditorAutomationProvider::EditorAutomationProvider(EditorCommandService& commands,
                                                   EditorTargetCoordinator& targets)
    : commands_(&commands), targets_(&targets) {
    session_.setCommandService(commands_);
    session_.setSessionId(SessionId("editor.mcp"));
}

EditorAutomationProvider::~EditorAutomationProvider() = default;

void EditorAutomationProvider::targetUnregistered(const TargetId& target) {
    if (session_.boundTargetId() != target) return;
    session_.clearRetainedPlans();
    session_.clearTarget();
}

std::string EditorAutomationProvider::invoke(const std::string& operation, const std::string& requestJson) {
    refreshProfile();
    EditorResult<EditorValue> parsed = editorValueFromJson(requestJson.empty() ? "{}" : requestJson);
    if (!parsed.ok() || parsed.value().type() != EditorValue::Type::Object)
        return errorJson(EditorStatus::Rejected, "editor.automation.invalid-json", "Request must be a JSON object");
    const auto& request = *parsed.value().getIf<EditorValue::Object>();
    if (operation == "commands") return commandsJson();
    if (operation == "target-create") return createTarget(request);
    if (operation == "target-close") return closeTarget(request);
    if (operation == "observe-start") return startObservation(request);
    if (operation == "observe-describe") return describeObservation(request);
    if (operation == "observe-publish") return publishObservation(request);
    if (operation == "observe-poll") return pollObservation(request);
    if (operation == "observe-close") return closeObservation(request);
    if (operation == "inspect") {
        auto result = targets_->inspect(TargetId(stringField(request, "target")));
        lastDiagnostics_ = result.diagnostics();
        EditorValue response = resultValue(result.code(), result.diagnostics());
        if (result.ok()) (*response.getIf<EditorValue::Object>())["target"] = result.value();
        return editorValueToJson(response);
    }
    if (operation == "plan" || operation == "execute") {
        auto bound = bindRequestedTarget(request);
        if (!bound.ok()) return editorValueToJson(resultValue(bound.code(), bound.diagnostics()));
    }
    if (operation == "plan") {
        const std::string command = stringField(request, "command");
        std::optional<Revision> expected;
        if (const auto* value = integerField(request, "expectedRevision")) expected = static_cast<Revision>(*value);
        auto result = session_.retainPlan(CommandId(command), valueField(request, "payload"),
                                          CommandSource::Automation, expected);
        lastDiagnostics_ = result.diagnostics();
        EditorValue response = resultValue(result.code(), result.diagnostics());
        if (result.ok()) (*response.getIf<EditorValue::Object>())["planId"] = result.value().value();
        return editorValueToJson(response);
    }
    if (operation == "commit") {
        auto result = session_.executeRetainedPlan(PlanId(stringField(request, "planId")), CommandSource::Automation);
        lastDiagnostics_ = result.diagnostics();
        return editorValueToJson(receiptValue(result));
    }
    if (operation == "execute") {
        const CommandId command(stringField(request, "command"));
        std::optional<EditorResult<TransactionReceipt>> result;
        if (commands_->supportsPlanning(command)) {
            auto planned = session_.planCommand(command, valueField(request, "payload"), CommandSource::Automation);
            if (!planned.ok()) {
                result.emplace(EditorResult<TransactionReceipt>::failure(planned.status()));
            } else {
                result.emplace(session_.executePlan(planned.value(), valueField(request, "payload"),
                                                    CommandSource::Automation));
            }
        } else {
            result.emplace(session_.executeCommandReceipt(command, valueField(request, "payload"),
                                                          CommandSource::Automation));
        }
        lastDiagnostics_ = result->diagnostics();
        return editorValueToJson(receiptValue(*result));
    }
    if (operation == "cancel") {
        auto result = session_.cancelRetainedPlan(PlanId(stringField(request, "planId")));
        lastDiagnostics_ = result.diagnostics();
        return editorValueToJson(resultValue(result.code(), result.diagnostics()));
    }
    if (operation == "undo" || operation == "redo") {
        const TargetId target(stringField(request, "target"));
        if (!target.empty()) {
            auto result = operation == "undo" ? targets_->undo(target) : targets_->redo(target);
            lastDiagnostics_ = result.diagnostics();
            return editorValueToJson(receiptValue(result));
        }
        const bool changed = operation == "undo" ? session_.transactions().undo() : session_.transactions().redo();
        EditorValue response = resultValue(changed ? EditorStatus::Applied : EditorStatus::NoOp, {});
        (*response.getIf<EditorValue::Object>())["changed"] = changed;
        return editorValueToJson(response);
    }
    if (operation == "diagnostics")
        return editorValueToJson(resultValue(EditorStatus::Applied, lastDiagnostics_));
    return errorJson(EditorStatus::Unsupported, "editor.automation.unsupported-operation",
                     "Unsupported editor automation operation: " + operation);
}

std::string EditorAutomationProvider::startObservation(const EditorValue::Object& request) {
    const EditorValue* descriptorValue = findField(request, "descriptor");
    const EditorValue* eventValue = findField(request, "event");
    const auto* descriptor = descriptorValue ? descriptorValue->getIf<EditorValue::Object>() : nullptr;
    const auto* event = eventValue ? eventValue->getIf<EditorValue::Object>() : nullptr;
    if (!descriptor || !event)
        return errorJson(EditorStatus::Rejected, "editor.automation.observe-start",
                         "Observation descriptor and initial event must be JSON objects");

    const std::string id = "editor.observe." + std::to_string(nextObservationSession_++);
    auto session = std::make_unique<ObservationSession>();
    session->descriptor = *descriptor;
    session->distinct.reset(session->source.distinctUntilChanged());
    ObservationSession* observed = session.get();
    session->subscription = session->distinct->subscribe(
        [observed](const std::string& value) { observed->pending.push_back(value); });
    session->source.onNext(editorValueToJson(*eventValue));
    // startObservation returns the initial event directly. Retain it only as
    // the distinct baseline so the first poll does not deliver it twice.
    session->pending.clear();
    observationSessions_.emplace(id, std::move(session));

    EditorValue response = resultValue(EditorStatus::Applied, {});
    auto* object = response.getIf<EditorValue::Object>();
    (*object)["sessionId"] = id;
    (*object)["event"] = *eventValue;
    return editorValueToJson(response);
}

std::string EditorAutomationProvider::describeObservation(const EditorValue::Object& request) const {
    const std::string id = stringField(request, "sessionId");
    const auto found = observationSessions_.find(id);
    if (found == observationSessions_.end())
        return errorJson(EditorStatus::NotFound, "editor.automation.observe-session", "Observation session not found");
    EditorValue response = resultValue(EditorStatus::Applied, {});
    (*response.getIf<EditorValue::Object>())["descriptor"] = found->second->descriptor;
    return editorValueToJson(response);
}

std::string EditorAutomationProvider::publishObservation(const EditorValue::Object& request) {
    const std::string id = stringField(request, "sessionId");
    const auto found = observationSessions_.find(id);
    if (found == observationSessions_.end())
        return errorJson(EditorStatus::NotFound, "editor.automation.observe-session", "Observation session not found");
    const EditorValue* event = findField(request, "event");
    if (!event || event->type() != EditorValue::Type::Object)
        return errorJson(EditorStatus::Rejected, "editor.automation.observe-event", "Observation event must be an object");
    found->second->source.onNext(editorValueToJson(*event));
    return editorValueToJson(resultValue(EditorStatus::Applied, {}));
}

std::string EditorAutomationProvider::pollObservation(const EditorValue::Object& request) {
    const std::string id = stringField(request, "sessionId");
    const auto found = observationSessions_.find(id);
    if (found == observationSessions_.end())
        return errorJson(EditorStatus::NotFound, "editor.automation.observe-session", "Observation session not found");
    EditorValue::Array events;
    for (const std::string& encoded : found->second->pending) {
        EditorResult<EditorValue> parsed = editorValueFromJson(encoded);
        if (parsed.ok()) events.push_back(std::move(parsed).takeValue());
    }
    found->second->pending.clear();
    EditorValue response = resultValue(EditorStatus::Applied, {});
    auto* object = response.getIf<EditorValue::Object>();
    (*object)["sessionId"] = id;
    (*object)["events"] = std::move(events);
    return editorValueToJson(response);
}

std::string EditorAutomationProvider::closeObservation(const EditorValue::Object& request) {
    const std::string id = stringField(request, "sessionId");
    const auto found = observationSessions_.find(id);
    if (found == observationSessions_.end())
        return errorJson(EditorStatus::NotFound, "editor.automation.observe-session", "Observation session not found");
    found->second->subscription.dispose();
    observationSessions_.erase(found);
    EditorValue response = resultValue(EditorStatus::Applied, {});
    (*response.getIf<EditorValue::Object>())["sessionId"] = id;
    return editorValueToJson(response);
}

std::string EditorAutomationProvider::createTarget(const EditorValue::Object& request) {
    const TargetId target(stringField(request, "target"));
    const std::string type = stringField(request, "type");
    if (target.empty())
        return errorJson(EditorStatus::Rejected, "editor.automation.target-id", "Target id must not be empty");
    if (ownedTargets_.contains(target))
        return errorJson(EditorStatus::Conflict, "editor.automation.target-exists", "Target already exists");

    std::optional<EditorResult<AutomationOwnedTarget>> created;
    const bool handled = eve::cap::forEachUntil<IEditorAutomationTargetFactory>(
        [&](IEditorAutomationTargetFactory* factory) {
            if (!factory->supports(type)) return false;
            created.emplace(factory->create(target, type, request));
            return true;
        });
    if (!handled) {
        return errorJson(EditorStatus::Unsupported, "editor.automation.target-type",
                         "No loaded editor adapter supports target type: " + type);
    }
    lastDiagnostics_ = created->diagnostics();
    if (!created->ok() || !created->value().target)
        return editorValueToJson(resultValue(created->code(), created->diagnostics()));

    auto registered = targets_->registerTarget(*created->value().target);
    lastDiagnostics_ = registered.diagnostics();
    if (!registered.ok()) return editorValueToJson(resultValue(registered.code(), registered.diagnostics()));
    ownedTargets_.emplace(target, std::move(*created).takeValue());
    EditorValue response = resultValue(EditorStatus::Applied, {});
    auto* value = response.getIf<EditorValue::Object>();
    (*value)["target"] = target.value();
    (*value)["type"] = type;
    return editorValueToJson(response);
}

std::string EditorAutomationProvider::closeTarget(const EditorValue::Object& request) {
    const TargetId target(stringField(request, "target"));
    auto found = ownedTargets_.find(target);
    if (found == ownedTargets_.end())
        return errorJson(EditorStatus::NotFound, "editor.automation.target-not-found",
                         "Owned automation target does not exist");
    auto unregistered = targets_->unregisterTarget(target);
    lastDiagnostics_ = unregistered.diagnostics();
    if (!unregistered.ok())
        return editorValueToJson(resultValue(unregistered.code(), unregistered.diagnostics()));
    targetUnregistered(target);
    ownedTargets_.erase(found);
    return editorValueToJson(resultValue(EditorStatus::Applied, {}));
}

void EditorAutomationProvider::refreshProfile() {
    HostProfile profile = HostProfile::automation();
    if (commands_)
        for (const CommandDescriptor& command : commands_->commands(HostProfile::developer()))
            if (command.automationAllowed && profile.hasFeatures(command.requiredFeatures))
                profile.allowCommand(command.id);
    session_.setHostProfile(std::move(profile));
}

std::string EditorAutomationProvider::commandsJson() {
    EditorValue::Array descriptors;
    for (const CommandDescriptor& command : session_.availableCommands()) {
        EditorValue::Object descriptor;
        descriptor["id"]          = command.id.value();
        descriptor["displayName"] = command.displayName;
        descriptor["category"]    = command.category;
        descriptor["owner"]       = command.ownerModule;
        descriptor["planned"]     = commands_->supportsPlanning(command.id);
        descriptors.emplace_back(std::move(descriptor));
    }
    EditorValue response = resultValue(EditorStatus::Applied, {});
    (*response.getIf<EditorValue::Object>())["commands"] = EditorValue(std::move(descriptors));
    return editorValueToJson(response);
}

EditorResult<void> EditorAutomationProvider::bindRequestedTarget(const EditorValue::Object& request) {
    const std::string target = stringField(request, "target");
    if (target.empty()) return eve::editing::applied<void>();
    return targets_->bind(session_, TargetId(target));
}

}  // namespace eve::editor
