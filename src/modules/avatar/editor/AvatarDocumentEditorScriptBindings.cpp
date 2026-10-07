#include "avatar/editor/AvatarDocumentEditorScriptBindings.h"

#include "avatar/editor/AvatarDocumentEditor.h"
#include "avatar/editor/AvatarEditorModule.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::avatar_editor {
namespace {

constexpr const char* kBindingSource = "editor.avatar.document.squirrel";

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
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto avatarEditor = editor::addScriptClass<ScriptAvatarDocumentEditor>(table, "AvatarDocumentEditor");
    avatarEditor.addFunc("configureWorkspace",
                         [bind](ScriptAvatarDocumentEditor* self, editor::EditorWorkspace* workspace) {
                             return bind.checked(
                                 self && workspace, "avatar editor and workspace must not be null",
                                 [&] { return self->editor().configureWorkspace(*workspace); }, "workspace");
                         });
    avatarEditor.addFunc("selectLayer", [bind](ScriptAvatarDocumentEditor* self, const std::string& id) {
        return bind.checked(self, "avatar editor must not be null", [&] { return self->editor().selectLayer(id); });
    });
    avatarEditor.addFunc("selectParameter", [bind](ScriptAvatarDocumentEditor* self, const std::string& id) {
        return bind.checked(self, "avatar editor must not be null", [&] { return self->editor().selectParameter(id); });
    });
    avatarEditor.addFunc("selectExpression", [bind](ScriptAvatarDocumentEditor* self, const std::string& id) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().selectExpression(id); });
    });
    avatarEditor.addFunc("pointerDown", [bind](ScriptAvatarDocumentEditor* self, float x, float y) {
        return bind.checked(self, "avatar editor must not be null", [&] { return self->editor().pointerDown(x, y); });
    });
    avatarEditor.addFunc("setLayerVisible", [bind](ScriptAvatarDocumentEditor* self, bool visible) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().setLayerVisible(visible); });
    });
    avatarEditor.addFunc("setLayerZ", [bind](ScriptAvatarDocumentEditor* self, int zIndex) {
        return bind.checked(self, "avatar editor must not be null", [&] { return self->editor().setLayerZ(zIndex); });
    });
    avatarEditor.addFunc("setParameterValue", [bind](ScriptAvatarDocumentEditor* self, float value) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().setParameterValue(static_cast<double>(value)); });
    });
    avatarEditor.addFunc("createLayer",
                         [bind](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             return bind.checked(self, "avatar editor must not be null",
                                                 [&] { return self->editor().createLayer(id, name); });
                         });
    avatarEditor.addFunc("deleteSelectedLayer", [bind](ScriptAvatarDocumentEditor* self) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().deleteSelectedLayer(); });
    });
    avatarEditor.addFunc("createParameter",
                         [bind](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             return bind.checked(self, "avatar editor must not be null",
                                                 [&] { return self->editor().createParameter(id, name); });
                         });
    avatarEditor.addFunc("deleteSelectedParameter", [bind](ScriptAvatarDocumentEditor* self) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().deleteSelectedParameter(); });
    });
    avatarEditor.addFunc("createExpression",
                         [bind](ScriptAvatarDocumentEditor* self, const std::string& id, const std::string& name) {
                             return bind.checked(self, "avatar editor must not be null",
                                                 [&] { return self->editor().createExpression(id, name); });
                         });
    avatarEditor.addFunc("deleteSelectedExpression", [bind](ScriptAvatarDocumentEditor* self) {
        return bind.checked(self, "avatar editor must not be null",
                            [&] { return self->editor().deleteSelectedExpression(); });
    });
    avatarEditor.addFunc("undo", [bind](ScriptAvatarDocumentEditor* self) {
        return bind.history(self, "avatar editor must not be null", [&] { return self->editor().undo(); });
    });
    avatarEditor.addFunc("redo", [bind](ScriptAvatarDocumentEditor* self) {
        return bind.history(self, "avatar editor must not be null", [&] { return self->editor().redo(); });
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
        return self && self->editor().isLayerSelected(index);
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
        return self && self->editor().isPreviewSelected(index);
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
        return self && self->editor().isParameterSelected(index);
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
        return self && self->editor().isExpressionSelected(index);
    });
    avatarEditor.addFunc("getExpressionChannelCount", [](ScriptAvatarDocumentEditor* self, int index) {
        return self ? self->editor().expressionChannelCount(index) : 0;
    });
    avatarEditor.addFunc("getExpressionChannelName",
                         [](ScriptAvatarDocumentEditor* self, int expression, int channel) {
                             return self ? self->editor().expressionChannelName(expression, channel) : std::string{};
                         });

    moduleClass.addFunc("create", [bind](AvatarEditorModule*, const std::string& targetId) {
        return bind.ownedCreate<ScriptAvatarDocumentEditor>("avatar target id must not be empty", targetId);
    });
}

}  // namespace eve::avatar_editor
