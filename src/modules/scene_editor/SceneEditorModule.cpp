#include "scene_editor/SceneEditorModule.h"

#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "scene_editing/SceneEditingCommands.h"
#include "scene_editing/SceneTarget.h"
#include "scene/Scene.h"

#include <simplesquirrel/simplesquirrel.hpp>

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
    bool supports(std::string_view type) const override { return type == "scene" || type == "scene-host"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type,
        const editor::EditorValue::Object& request) override {
        editor::AutomationOwnedTarget owned;
        if (type == "scene") {
            auto sceneTarget = std::make_unique<scene_editing::SceneDocumentTarget>(target.value());
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
        owned.target = std::make_unique<scene_editing::SceneHostEditorTarget>(target.value(), hostResult.value());
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(SceneEditorModule, new SceneEditorModule());

SceneEditorModule::SceneEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !scene_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register scene editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

SceneEditorModule::~SceneEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("scene_editing").ignore("scene editor adapter shutdown");
}

void SceneEditorModule::expose(ssq::Table& table) { table.addClass(name, SceneEditorModule::create, false); }
void SceneEditorModule::expose(ssq::Class&) {}

}  // namespace eve::scene_editor
