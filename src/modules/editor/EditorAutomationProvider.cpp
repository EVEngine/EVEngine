#include "editor/EditorAutomationProvider.h"

#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorMaterialTarget.h"
#include "editor/EditorSceneTarget.h"
#include "editor/EditorValueJson.h"
#include "rx/Rx.h"
#ifdef EVENGINE_HAS_SCENE
#include "scene/Scene.h"
#endif
#ifdef EVENGINE_HAS_GRAPHICS
#include "graphics/RenderSystem3D.h"
#endif

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

#ifdef EVENGINE_HAS_GRAPHICS
class AutomationMaterialAssetResolver final : public IMaterialRuntimeAssetResolver {
public:
    EditorResult<graphics::Texture*> resolveTexture(const std::string& asset) const override {
        return unresolved<graphics::Texture>(asset);
    }
    EditorResult<graphics::Shader*> resolveShader(const std::string& asset) const override {
        return unresolved<graphics::Shader>(asset);
    }

private:
    template <class T>
    static EditorResult<T*> unresolved(const std::string& asset) {
        EditorResult<T*> result;
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back({RuleId("editor.automation.material-asset"),
                                      DiagnosticSeverity::Error,
                                      "Automation binding cannot resolve material asset: " + asset});
        return result;
    }
};

graphics::Renderable3D* findRenderable(std::uint32_t id, std::uint32_t generation) {
    ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(graphics::Renderable3D)),
                             id, generation};
    return dynamic_cast<graphics::Renderable3D*>(ecs::try_get(handle));
}

EditorValue runtimeMaterialSnapshot(const graphics::Renderable3D::MeshRenderer& renderer) {
    MaterialDocumentTarget defaults("material.runtime.seed");
    EditorValue snapshot = defaults.snapshotValue();
    auto* root = snapshot.getIf<EditorValue::Object>();
    auto* properties = root ? root->at("properties").getIf<EditorValue::Object>() : nullptr;
    (*properties)["shading.tint"] = EditorValue::Array{double(renderer.r), double(renderer.g),
                                                       double(renderer.b), double(renderer.a)};
    (*properties)["shading.metallic"] = double(renderer.metallic);
    (*properties)["shading.roughness"] = double(renderer.roughness);
    (*properties)["parallax.scale"] = double(renderer.parallaxScale);
    (*properties)["lighting.receive"] = renderer.receiveLight;
    (*properties)["shadow.cast"] = renderer.castShadow;
    (*properties)["shadow.receive"] = renderer.receiveShadow;
    return snapshot;
}
#endif

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
    IEditableTarget* bound = session_.target();
    if (!bound || bound->targetId() != target.value()) return;
    session_.clearRetainedPlans();
    session_.bindTarget(nullptr);
}

std::string EditorAutomationProvider::invoke(const std::string& operation, const std::string& requestJson) {
    refreshProfile();
    EditorResult<EditorValue> parsed = editorValueFromJson(requestJson.empty() ? "{}" : requestJson);
    if (!parsed.isAccepted() || !parsed.value || parsed.value->type() != EditorValue::Type::Object)
        return errorJson(EditorStatus::Rejected, "editor.automation.invalid-json", "Request must be a JSON object");
    const auto& request = *parsed.value->getIf<EditorValue::Object>();
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
        lastDiagnostics_ = result.diagnostics;
        EditorValue response = resultValue(result.status, result.diagnostics);
        if (result.value) (*response.getIf<EditorValue::Object>())["target"] = std::move(*result.value);
        return editorValueToJson(response);
    }
    if (operation == "plan" || operation == "execute") {
        auto bound = bindRequestedTarget(request);
        if (!bound.isAccepted()) return editorValueToJson(resultValue(bound.status, bound.diagnostics));
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
            if (!planned.isAccepted() || !planned.value) {
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
            auto result = operation == "undo" ? targets_->undo(target) : targets_->redo(target);
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
        if (parsed.isAccepted() && parsed.value) events.push_back(std::move(*parsed.value));
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

    std::unique_ptr<IEditableTarget>       owned;
    std::unique_ptr<IMaterialRuntimeSink> ownedMaterialSink;
    if (type == "scene") {
        auto scene = std::make_unique<SceneDocumentTarget>(target.value());
        const std::string object = stringField(request, "object");
        if (!object.empty()) {
            CreateSceneObjectRequest create;
            create.id = ObjectId(object);
            create.name = object;
            auto operation = scene->makeCreate(create);
            if (!operation.isAccepted() || !operation.value)
                return editorValueToJson(resultValue(operation.status, operation.diagnostics));
            auto applied = scene->applyDomainOperation(*operation.value);
            if (!applied.isAccepted()) return editorValueToJson(resultValue(applied.status, applied.diagnostics));
        }
        owned = std::move(scene);
    } else if (type == "scene-host") {
        const std::string host = stringField(request, "host");
        if (host.empty())
            return errorJson(EditorStatus::Rejected, "editor.automation.scene-host-name",
                             "Live scene target requires a host name");
#ifdef EVENGINE_HAS_SCENE
        auto* scene = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        if (!scene)
            return errorJson(EditorStatus::Unsupported, "editor.automation.scene-module-unavailable",
                             "Scene module is not available");
        auto hostResult = scene->findHost(host);
        if (!hostResult.ok() || !hostResult.value())
            return errorJson(EditorStatus::NotFound, "editor.automation.scene-host-not-found",
                             "Live SceneHost does not exist: " + host);
        owned = std::make_unique<SceneHostEditorTarget>(target.value(), hostResult.value());
#else
        return errorJson(EditorStatus::Unsupported, "editor.automation.scene-module-unavailable",
                         "Scene module is not available in this build");
#endif
    } else if (type == "material") {
        owned = std::make_unique<MaterialDocumentTarget>(target.value());
    } else if (type == "material-renderable3d") {
#ifdef EVENGINE_HAS_GRAPHICS
        const auto* id = integerField(request, "entityId");
        const auto* generation = integerField(request, "generation");
        if (!id || !generation || *id < 0 || *generation < 0 ||
            *id > UINT32_MAX || *generation > UINT32_MAX)
            return errorJson(EditorStatus::Rejected, "editor.automation.material-handle",
                             "Live material target requires uint32 entityId and generation");
        auto* renderable = findRenderable(static_cast<std::uint32_t>(*id),
                                          static_cast<std::uint32_t>(*generation));
        if (!renderable)
            return errorJson(EditorStatus::Conflict, "editor.automation.material-stale",
                             "Renderable3D handle is missing or stale");
        auto renderer = renderable->meshRenderer();
        if (renderer->material || renderer->texture || renderer->normalTexture ||
            renderer->heightTexture || renderer->shader)
            return errorJson(EditorStatus::Unsupported, "editor.automation.material-assets",
                             "Live material binding currently supports field-backed materials without asset overrides");
        static const AutomationMaterialAssetResolver assets;
        auto sink = std::make_unique<Renderable3DMaterialRuntimeSink>(renderable, &assets);
        auto publishing = std::make_unique<MaterialPublishingTarget>(target.value(), sink.get());
        auto loaded = publishing->authoringTarget().loadSnapshot(runtimeMaterialSnapshot(*renderer));
        if (!loaded.isAccepted())
            return editorValueToJson(resultValue(loaded.status, loaded.diagnostics));
        ownedMaterialSink = std::move(sink);
        owned = std::move(publishing);
#else
        return errorJson(EditorStatus::Unsupported, "editor.automation.graphics-module-unavailable",
                         "Graphics module is not available in this build");
#endif
    } else {
        return errorJson(EditorStatus::Unsupported, "editor.automation.target-type",
                         "Target type must be scene, scene-host, material or material-renderable3d");
    }

    auto registered = targets_->registerTarget(*owned);
    lastDiagnostics_ = registered.diagnostics;
    if (!registered.isAccepted()) return editorValueToJson(resultValue(registered.status, registered.diagnostics));
    if (ownedMaterialSink) ownedMaterialSinks_.emplace(target, std::move(ownedMaterialSink));
    ownedTargets_.emplace(target, std::move(owned));
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
    lastDiagnostics_ = unregistered.diagnostics;
    if (!unregistered.isAccepted())
        return editorValueToJson(resultValue(unregistered.status, unregistered.diagnostics));
    targetUnregistered(target);
    ownedTargets_.erase(found);
    ownedMaterialSinks_.erase(target);
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
    if (target.empty()) return EditorResult<void>::applied();
    return targets_->bind(session_, TargetId(target));
}

}  // namespace eve::editor
