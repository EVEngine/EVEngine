#include "scene/editor/SceneEditorScriptBindings.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "scene/Scene.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/SceneEditorSession.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::scene_editor {
void exposeSceneEditorSessions(ssq::Table& table, ssq::Class& module) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<SceneEditorSession>(
        "SceneEditorSession", std::function<SceneEditorSession*()>([]() -> SceneEditorSession* { return nullptr; }),
        true);
    const auto project = [vm](editing::Result<editing::TransactionReceipt> result) {
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    };
    cls.addFunc("execute", [vm, project](SceneEditorSession* self, std::string command, ssq::Object payload) {
        auto value = script::valueFromSquirrel(payload);
        if (!value.ok()) return script::projectStatusResult(vm, value.status(), false, false);
        return project(self->execute(std::move(command), editing::toEditingValue(value.value())));
    });
    cls.addFunc("undo", [project](SceneEditorSession* self) { return project(self->undo()); });
    cls.addFunc("restrictCommands", [vm](SceneEditorSession* self, ssq::Array commands) {
        std::vector<std::string> ids;
        for (SQInteger i = 0; i < commands.size(); ++i) ids.push_back(commands.get<std::string>(i));
        auto result = self->restrictCommands(ids);
        return script::projectStatusResult(vm, result.status(), result.ok(), false);
    });
    cls.addFunc("redo", [project](SceneEditorSession* self) { return project(self->redo()); });
    cls.addFunc("saveJson", &SceneEditorSession::saveJson);
    cls.addFunc("restoreJson", [project](SceneEditorSession* self, const std::string& json) {
        return project(self->restoreJson(json));
    });
    cls.addFunc("snapshot", [vm](SceneEditorSession* self) {
        return script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, true,
                                           editing::toPresentationValue(self->snapshot()));
    });
    cls.addFunc("getRevision", &SceneEditorSession::revision);
    module.addFunc("createSession", [vm](SceneEditorModule*, const std::string& id) {
        if (id.empty())
            return script::projectStatusResult(vm,
                                               Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                 "Scene target id must not be empty")),
                                               false, false);
        auto object =
            script::makeOwnedSquirrelInstance<SceneEditorSession>(vm, std::make_unique<SceneEditorSession>(id));
        if (!object.ok()) return script::projectStatusResult(vm, object.status(), false, false);
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", std::move(object).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    module.addFunc("createLiveSession", [vm](SceneEditorModule*, const std::string& id, const std::string& hostName) {
        auto* scene = ModuleManager::getInstance<scene::Scene>("Scene");
        if (!scene || id.empty())
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Live scene editing requires Scene module and a nonempty target id")),
                false, false);
        auto host = scene->findHost(hostName);
        if (!host.ok()) return script::projectStatusResult(vm, host.status(), false, false);
        auto object = script::makeOwnedSquirrelInstance<SceneEditorSession>(
            vm, std::make_unique<SceneEditorSession>(
                    std::make_unique<scene_editing::SceneHostEditorTarget>(id, host.value())));
        if (!object.ok()) return script::projectStatusResult(vm, object.status(), false, false);
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", std::move(object).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
}
}  // namespace eve::scene_editor
