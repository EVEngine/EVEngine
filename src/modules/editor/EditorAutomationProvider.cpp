#include "editor/EditorAutomationProvider.h"

#include "editor/EditorAuthoringService.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorValueJson.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace eve::editor {
namespace {

const char* statusName(EditorStatus status) {
    switch (status) {
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
        value["rule"]     = diagnostic.rule.value();
        value["severity"] = static_cast<std::int64_t>(diagnostic.severity);
        value["message"]  = diagnostic.message;
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
    EditorValue response = resultValue(result.status, result.diagnostics);
    if (result.value) {
        auto* object                = response.getIf<EditorValue::Object>();
        (*object)["transactionId"]  = result.value->id.value();
        (*object)["state"]          = transactionStateName(result.value->state);
        (*object)["beforeRevision"] = static_cast<std::int64_t>(result.value->beforeRevision);
        (*object)["afterRevision"]  = static_cast<std::int64_t>(result.value->afterRevision);
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
    return editorValueToJson(resultValue(status, {{RuleId(rule), DiagnosticSeverity::Error, std::move(message)}}));
}

}  // namespace

EditorAutomationProvider::EditorAutomationProvider(EditorCommandService& commands,
                                                   EditorAuthoringService& authoring)
    : commands_(&commands), authoring_(&authoring) {
    session_.setCommandService(commands_);
    session_.setSessionId(SessionId("editor.mcp"));
}

void EditorAutomationProvider::targetUnregistered(const TargetId& target) {
    IEditableTarget* bound = session_.target();
    if (!bound || bound->targetId() != target.value()) return;
    session_.clearRetainedPlans();
    session_.bindTarget(nullptr);
}

std::string EditorAutomationProvider::invoke(const std::string& operation, const std::string& requestJson) {
    refreshProfile();
    EditorResult<EditorValue> parsed = editorValueFromJson(requestJson.empty() ? "{}" : requestJson);
    if (!parsed.accepted() || !parsed.value || parsed.value->type() != EditorValue::Type::Object)
        return errorJson(EditorStatus::Rejected, "editor.automation.invalid-json", "Request must be a JSON object");
    const auto& request = *parsed.value->getIf<EditorValue::Object>();
    if (operation == "commands") return commandsJson();
    if (operation == "inspect") {
        auto result = authoring_->inspect(TargetId(stringField(request, "target")));
        lastDiagnostics_ = result.diagnostics;
        EditorValue response = resultValue(result.status, result.diagnostics);
        if (result.value) (*response.getIf<EditorValue::Object>())["target"] = std::move(*result.value);
        return editorValueToJson(response);
    }
    if (operation == "plan" || operation == "execute") {
        auto bound = bindRequestedTarget(request);
        if (!bound.accepted()) return editorValueToJson(resultValue(bound.status, bound.diagnostics));
    }
    if (operation == "plan") {
        const std::string command = stringField(request, "command");
        std::optional<Revision> expected;
        if (const auto* value = integerField(request, "expectedRevision")) expected = static_cast<Revision>(*value);
        auto result = session_.retainPlan(CommandId(command), valueField(request, "payload"),
                                          CommandSource::Automation, expected);
        lastDiagnostics_ = result.diagnostics;
        EditorValue response = resultValue(result.status, result.diagnostics);
        if (result.value) (*response.getIf<EditorValue::Object>())["planId"] = result.value->value();
        return editorValueToJson(response);
    }
    if (operation == "commit") {
        auto result = session_.executeRetainedPlan(PlanId(stringField(request, "planId")), CommandSource::Automation);
        lastDiagnostics_ = result.diagnostics;
        return editorValueToJson(receiptValue(result));
    }
    if (operation == "execute") {
        const CommandId command(stringField(request, "command"));
        EditorResult<TransactionReceipt> result;
        if (commands_->supportsPlanning(command)) {
            auto planned = session_.planCommand(command, valueField(request, "payload"), CommandSource::Automation);
            if (!planned.accepted() || !planned.value) {
                result.status      = planned.status;
                result.diagnostics = std::move(planned.diagnostics);
            } else {
                result = session_.executePlan(*planned.value, valueField(request, "payload"), CommandSource::Automation);
            }
        } else {
            result = session_.executeCommandReceipt(command, valueField(request, "payload"),
                                                    CommandSource::Automation);
        }
        lastDiagnostics_ = result.diagnostics;
        return editorValueToJson(receiptValue(result));
    }
    if (operation == "cancel") {
        auto result = session_.cancelRetainedPlan(PlanId(stringField(request, "planId")));
        lastDiagnostics_ = result.diagnostics;
        return editorValueToJson(resultValue(result.status, result.diagnostics));
    }
    if (operation == "undo" || operation == "redo") {
        const TargetId target(stringField(request, "target"));
        if (!target.empty()) {
            auto result = operation == "undo" ? authoring_->undo(target) : authoring_->redo(target);
            lastDiagnostics_ = result.diagnostics;
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
    if (target.empty()) return EditorResult<void>::applied();
    return authoring_->bind(session_, TargetId(target));
}

}  // namespace eve::editor
