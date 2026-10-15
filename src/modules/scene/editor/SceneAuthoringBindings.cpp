#include "scene/editor/SceneAuthoringBindings.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "scene/editing/SceneAuthoring.h"
#include "scene/editing/SceneAuthoringDocument.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/SceneEditorModule.h"
#include "scene/editor/SceneEditorSession.h"

namespace eve::scene_editor {
void exposeSceneAuthoringQueries(ssq::Table& table, ssq::Class& module) {
    const auto vm = table.getHandle();
    module.addFunc("createAuthoringSession", [vm](SceneEditorModule*, const std::string& id) {
        if (id.empty())
            return script::projectStatusResult(
                vm, Status::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "Authoring target id must be nonempty")));
        auto owned = script::makeOwnedSquirrelInstance<SceneEditorSession>(
            vm,
            std::make_unique<SceneEditorSession>(std::make_unique<scene_editing::SceneAuthoringDocumentTarget>(id)));
        if (!owned.ok()) return script::projectStatusResult(vm, owned.status());
        auto result =
            script::projectStatusResult(vm, Status::success(StatusCode::Applied), std::move(owned).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
    module.addFunc("inspectAuthoringSnapshot", [vm](SceneEditorModule*, ssq::Object snapshot, const std::string& id,
                                                    ssq::Object bounds) {
        auto decoded = script::valueFromSquirrel(snapshot);
        if (!decoded.ok()) return script::projectStatusResult(vm, decoded.status());
        scene_editing::SceneDocumentTarget target("authoring-query");
        auto                               hierarchy = editing::toEditingValue(decoded.value());
        if (const auto* root = hierarchy.getIf<editing::Value::Object>(); root && root->contains("hierarchy")) {
            auto copied = root->at("hierarchy");
            hierarchy   = std::move(copied);
        }
        auto restore = target.makeRestore(hierarchy);
        if (!restore.ok()) return script::projectStatusResult(vm, restore.status());
        auto restored = target.applyDomainOperation(restore.value());
        if (!restored.ok()) return script::projectStatusResult(vm, restored.status());
        scene_editing::AuthoringBounds local{};
        auto                           decodedBounds = script::valueFromSquirrel(bounds);
        if (!decodedBounds.ok()) return script::projectStatusResult(vm, decodedBounds.status());
        const auto* values = decodedBounds.value().getIf<Value::Array>();
        if (!values || values->size() != 6)
            return script::projectStatusResult(
                vm, Status::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "Authoring bounds require six numbers")));
        for (std::size_t i = 0; i < local.size(); ++i) {
            if (const auto* real = (*values)[i].getIf<double>())
                local[i] = *real;
            else if (const auto* integer = (*values)[i].getIf<std::int64_t>())
                local[i] = static_cast<double>(*integer);
            else
                return script::projectStatusResult(
                    vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "Authoring bounds require finite numbers")));
        }
        auto inspected = scene_editing::inspectAuthoringObject(target, editing::ObjectId(id), local);
        if (!inspected.ok()) return script::projectStatusResult(vm, inspected.status());
        return script::projectStatusResult(vm, inspected.status(), editing::toPresentationValue(inspected.value()));
    });
}
}  // namespace eve::scene_editor
