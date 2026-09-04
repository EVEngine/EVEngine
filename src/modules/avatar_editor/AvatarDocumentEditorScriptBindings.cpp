#include "avatar_editor/AvatarDocumentEditorScriptBindings.h"

#include "avatar_editor/AvatarDocumentEditor.h"
#include "avatar_editor/AvatarEditorModule.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::avatar_editor {
namespace {

constexpr const char* kBindingSource = "editor.avatar.document.squirrel";

Status statusFrom(const avatar_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const avatar_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const avatar_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const avatar_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

class ScriptAvatarDocumentEditor {
public:
    explicit ScriptAvatarDocumentEditor(std::string targetId) : editor_(std::move(targetId)) {}

    AvatarDocumentEditor&       editor() noexcept { return editor_; }
    const AvatarDocumentEditor& editor() const noexcept { return editor_; }

private:
    AvatarDocumentEditor editor_;
};

}  // namespace

void exposeAvatarDocumentEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm = table.getHandle();
    auto avatarEditor    = table.addClass<ScriptAvatarDocumentEditor>(
        "AvatarDocumentEditor",
        std::function<ScriptAvatarDocumentEditor*()>([]() -> ScriptAvatarDocumentEditor* { return nullptr; }), true);

    avatarEditor.addFunc("configureWorkspace",
                         [vm](ScriptAvatarDocumentEditor* self, editor::EditorWorkspace* workspace) {
                             if (!self || !workspace)
                                 return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                       "avatar editor and workspace must not be null", "workspace");
                             return project(vm, self->editor().configureWorkspace(*workspace));
                         });
    avatarEditor.addFunc("selectLayer", [vm](ScriptAvatarDocumentEditor* self, const std::string& id) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().selectLayer(id));
    });
    avatarEditor.addFunc("selectParameter", [vm](ScriptAvatarDocumentEditor* self, const std::string& id) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().selectParameter(id));
    });
    avatarEditor.addFunc("selectExpression", [vm](ScriptAvatarDocumentEditor* self, const std::string& id) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().selectExpression(id));
    });
    avatarEditor.addFunc("pointerDown", [vm](ScriptAvatarDocumentEditor* self, float x, float y) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().pointerDown(x, y));
    });
    avatarEditor.addFunc("setLayerVisible", [vm](ScriptAvatarDocumentEditor* self, bool visible) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().setLayerVisible(visible));
    });
    avatarEditor.addFunc("setLayerZ", [vm](ScriptAvatarDocumentEditor* self, int zIndex) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().setLayerZ(zIndex));
    });
    avatarEditor.addFunc("setParameterValue", [vm](ScriptAvatarDocumentEditor* self, float value) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().setParameterValue(static_cast<double>(value)));
    });
    avatarEditor.addFunc("createLayer",
                         [vm](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             if (!self)
                                 return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                       "avatar editor must not be null");
                             return project(vm, self->editor().createLayer(id, name));
                         });
    avatarEditor.addFunc("deleteSelectedLayer", [vm](ScriptAvatarDocumentEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().deleteSelectedLayer());
    });
    avatarEditor.addFunc("createParameter",
                         [vm](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             if (!self)
                                 return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                       "avatar editor must not be null");
                             return project(vm, self->editor().createParameter(id, name));
                         });
    avatarEditor.addFunc("deleteSelectedParameter", [vm](ScriptAvatarDocumentEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().deleteSelectedParameter());
    });
    avatarEditor.addFunc("createExpression",
                         [vm](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             if (!self)
                                 return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                       "avatar editor must not be null");
                             return project(vm, self->editor().createExpression(id, name));
                         });
    avatarEditor.addFunc("deleteSelectedExpression", [vm](ScriptAvatarDocumentEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        return project(vm, self->editor().deleteSelectedExpression());
    });
    avatarEditor.addFunc("undo", [vm](ScriptAvatarDocumentEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    avatarEditor.addFunc("redo", [vm](ScriptAvatarDocumentEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    avatarEditor.addFunc("canUndo", [](ScriptAvatarDocumentEditor* self) { return self && self->editor().canUndo(); });
    avatarEditor.addFunc("canRedo", [](ScriptAvatarDocumentEditor* self) { return self && self->editor().canRedo(); });
    avatarEditor.addFunc("getRevision", [](ScriptAvatarDocumentEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
    });
    avatarEditor.addFunc("getPreviewRevision", [](ScriptAvatarDocumentEditor* self) {
        return self ? static_cast<int>(self->editor().previewRevision()) : 0;
    });
    avatarEditor.addFunc("getKind", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().kind() : std::string{};
    });
    avatarEditor.addFunc("getSelectedId", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().selectedId() : std::string{};
    });
    avatarEditor.addFunc("getSelectedType", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().selectedType() : std::string{};
    });
    avatarEditor.addFunc("getLayerCount", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().layerCount() : 0;
    });
    avatarEditor.addFunc("getLayerId", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().layerId(index) : std::string{};
    });
    avatarEditor.addFunc("getLayerName", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().layerName(index) : std::string{};
    });
    avatarEditor.addFunc("getLayerVisible", [](ScriptAvatarDocumentEditor* self, int index) {
        return self && self->editor().layerVisible(index);
    });
    avatarEditor.addFunc("getLayerZ", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().layerZ(index) : 0;
    });
    avatarEditor.addFunc("getLayerSelected", [](ScriptAvatarDocumentEditor* self, int index) {
        return self && self->editor().layerSelected(index);
    });
    avatarEditor.addFunc("getPreviewLayerCount", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().previewLayerCount() : 0;
    });
    avatarEditor.addFunc("getPreviewX", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewX(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewY", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewY(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewW", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewW(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewH", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewH(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewR", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewR(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewG", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewG(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewB", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewB(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewA", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewA(index) : 0.0f;
    });
    avatarEditor.addFunc("getPreviewSelected", [](ScriptAvatarDocumentEditor* self, int index) {
        return self && self->editor().previewSelected(index);
    });
    avatarEditor.addFunc("getPreviewName", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().previewName(index) : std::string{};
    });
    avatarEditor.addFunc("getParameterCount", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().parameterCount() : 0;
    });
    avatarEditor.addFunc("getParameterId", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().parameterId(index) : std::string{};
    });
    avatarEditor.addFunc("getParameterName", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().parameterName(index) : std::string{};
    });
    avatarEditor.addFunc("getParameterValue", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().parameterValue(index) : 0.0f;
    });
    avatarEditor.addFunc("getParameterMinimum", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().parameterMinimum(index) : 0.0f;
    });
    avatarEditor.addFunc("getParameterMaximum", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().parameterMaximum(index) : 1.0f;
    });
    avatarEditor.addFunc("getParameterSelected", [](ScriptAvatarDocumentEditor* self, int index) {
        return self && self->editor().parameterSelected(index);
    });
    avatarEditor.addFunc("getExpressionCount", [](ScriptAvatarDocumentEditor* self) {
        return self ? self->editor().expressionCount() : 0;
    });
    avatarEditor.addFunc("getExpressionId", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().expressionId(index) : std::string{};
    });
    avatarEditor.addFunc("getExpressionName", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().expressionName(index) : std::string{};
    });
    avatarEditor.addFunc("getExpressionSelected", [](ScriptAvatarDocumentEditor* self, int index) {
        return self && self->editor().expressionSelected(index);
    });
    avatarEditor.addFunc("getExpressionChannelCount", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().expressionChannelCount(index) : 0;
    });
    avatarEditor.addFunc("getExpressionChannelName",
                         [](ScriptAvatarDocumentEditor* self, int expression, int channel) {
                             return self ? self->editor().expressionChannelName(expression, channel) : std::string{};
                         });

    moduleClass.addFunc("create", [vm](AvatarEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "avatar target id must not be empty",
                                  "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptAvatarDocumentEditor>(
            vm, std::make_unique<ScriptAvatarDocumentEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::avatar_editor
