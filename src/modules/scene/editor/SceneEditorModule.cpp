#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/SceneEditorScriptBindings.h"
#include "scene/editor/ScriptComponentPayloadProvider.h"

#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "scene/Scene.h"
#include "scene/editing/SceneComponentPayload.h"
#include "scene/editing/SceneEditingCommands.h"
#include "scene/editing/SceneTarget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <map>
#include <stdexcept>
#include <string>

namespace eve::scene_editor {
namespace {

std::string stringField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    if (found == request.end()) return {};
    const auto* value = found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

}  // namespace

class SceneEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    TargetFactory(scene_editing::SceneComponentPayloadRegistry* payloads, std::map<std::string, std::string>* hosts)
        : payloads_(payloads), hosts_(hosts) {}

    bool supports(std::string_view type) const override { return type == "scene" || type == "scene-host"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type,
        const editor::EditorValue::Object& request) override {
        editor::AutomationOwnedTarget owned;
        if (type == "scene") {
            auto sceneTarget = std::make_unique<scene_editing::SceneDocumentTarget>(target.value());
            sceneTarget->bindComponentPayloads(payloads_);
            const std::string object = stringField(request, "object");
            if (!object.empty()) {
                scene_editing::CreateSceneObjectRequest create;
                create.id   = editor::ObjectId(object);
                create.name = object;
                auto operation = sceneTarget->makeCreate(create);
                if (!operation.ok())
                    return editor::EditorResult<editor::AutomationOwnedTarget>::failure(
                        operation.status());
                auto applied = sceneTarget->applyDomainOperation(operation.value());
                if (!applied.ok())
                    return editor::EditorResult<editor::AutomationOwnedTarget>::failure(
                        applied.status());
            }
            owned.target = std::move(sceneTarget);
            return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
        }

        const std::string host = stringField(request, "host");
        if (host.empty())
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::Rejected, editor::RuleId("editor.automation.scene-host-name"),
                "Live scene target requires a host name");
        auto* sceneModule = eve::ModuleManager::getInstance<eve::scene::Scene>("Scene");
        if (!sceneModule)
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::Unsupported, editor::RuleId("editor.automation.scene-module-unavailable"),
                "Scene module is not available");
        auto hostResult = sceneModule->findHost(host);
        if (!hostResult.ok() || !hostResult.value())
            return eve::editing::failed<editor::AutomationOwnedTarget>(
                editor::EditorStatus::NotFound, editor::RuleId("editor.automation.scene-host-not-found"),
                "Live SceneHost does not exist: " + host);
        auto liveTarget = std::make_unique<scene_editing::SceneHostEditorTarget>(target.value(), hostResult.value());
        liveTarget->bindComponentPayloads(payloads_);
        // 目标 id 是调用方选的，宿主名只有这里知道：记下来给组件 payload 提供方做映射
        // （`ScriptComponentPayloadProvider::HostResolver`）。目标关闭时留着这一条也无害——
        // 同名目标再创建会覆盖它，而查询一个不存在的宿主只会得到空组件表。
        (*hosts_)[target.value()] = host;
        owned.target              = std::move(liveTarget);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }

private:
    scene_editing::SceneComponentPayloadRegistry* payloads_ = nullptr;
    std::map<std::string, std::string>*           hosts_    = nullptr;
};

Module_IMPL(SceneEditorModule, new SceneEditorModule());

SceneEditorModule::SceneEditorModule()
    : payloads_(std::make_unique<scene_editing::SceneComponentPayloadRegistry>()),
      scriptComponents_(std::make_unique<ScriptComponentPayloadProvider>([this](const scene_editing::TargetId& t) {
          const auto found = targetHosts_.find(t.value());
          return found == targetHosts_.end() ? std::string{} : found->second;
      })),
      factory_(std::make_unique<TargetFactory>(payloads_.get(), &targetHosts_)) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !scene_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register scene editing commands");
    payloads_->registerProvider(scriptComponents_.get())
        .ignore("this module owns both the registry and the provider; a duplicate can only come from a bug here");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

SceneEditorModule::~SceneEditorModule() {
    payloads_->unregisterProvider(scriptComponents_.get());
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("scene_editing").ignore("scene editor adapter shutdown");
}

void SceneEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, SceneEditorModule::create, false);
    exposeSceneEditorSessions(table, module);
}
void SceneEditorModule::expose(ssq::Class&) {}

}  // namespace eve::scene_editor
