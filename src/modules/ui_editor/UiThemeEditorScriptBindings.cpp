#include "ui_editor/UiThemeEditorScriptBindings.h"

#include "ui_editor/UiEditorModule.h"
#include "ui_editor/UiThemeEditor.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::ui_editor {
namespace {

constexpr const char* kBindingSource = "editor.ui.theme.squirrel";

Status statusFrom(const ui_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const ui_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const ui_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const ui_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

class ScriptUiThemeEditor {
public:
    explicit ScriptUiThemeEditor(std::string targetId) : editor_(std::move(targetId)) {}

    UiThemeEditor&       editor() noexcept { return editor_; }
    const UiThemeEditor& editor() const noexcept { return editor_; }

private:
    UiThemeEditor editor_;
};

}  // namespace

void exposeUiThemeEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm = table.getHandle();
    auto themeEditor     = table.addClass<ScriptUiThemeEditor>(
        "UiThemeEditor", std::function<ScriptUiThemeEditor*()>([]() -> ScriptUiThemeEditor* { return nullptr; }),
        true);

    themeEditor.addFunc("configureWorkspace",
                        [vm](ScriptUiThemeEditor* self, editor::EditorWorkspace* workspace) {
                            if (!self || !workspace)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "theme editor and workspace must not be null", "workspace");
                            return project(vm, self->editor().configureWorkspace(*workspace));
                        });
    themeEditor.addFunc("selectTheme", [vm](ScriptUiThemeEditor* self, const std::string& id) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().selectTheme(id));
    });
    themeEditor.addFunc("createFromPreset", [vm](ScriptUiThemeEditor* self, const std::string& id,
                                                 const std::string& name, const std::string& preset) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().createFromPreset(id, name, preset));
    });
    themeEditor.addFunc("duplicateSelected",
                        [vm](ScriptUiThemeEditor* self, const std::string& id, const std::string& name) {
                            if (!self)
                                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                      "theme editor must not be null");
                            return project(vm, self->editor().duplicateSelected(id, name));
                        });
    themeEditor.addFunc("deleteSelected", [vm](ScriptUiThemeEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().deleteSelected());
    });
    themeEditor.addFunc("setActiveSelected", [vm](ScriptUiThemeEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().setActiveSelected());
    });
    themeEditor.addFunc("resetSelectedToBase", [vm](ScriptUiThemeEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().resetSelectedToBase());
    });
    themeEditor.addFunc("setColor", [vm](ScriptUiThemeEditor* self, const std::string& path, float r, float g,
                                         float b, float a) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().setToken(path, ui_editing::EditorValue::Array{
                                                             static_cast<double>(r), static_cast<double>(g),
                                                             static_cast<double>(b), static_cast<double>(a)}));
    });
    themeEditor.addFunc("setFloat", [vm](ScriptUiThemeEditor* self, const std::string& path, float value) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().setToken(path, static_cast<double>(value)));
    });
    themeEditor.addFunc("applyPreviewHost", [vm](ScriptUiThemeEditor* self, const std::string& hostName) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        return project(vm, self->editor().applyPreviewHost(hostName));
    });
    themeEditor.addFunc("undo", [vm](ScriptUiThemeEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    themeEditor.addFunc("redo", [vm](ScriptUiThemeEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result,
                       Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    themeEditor.addFunc("canUndo", [](ScriptUiThemeEditor* self) { return self && self->editor().canUndo(); });
    themeEditor.addFunc("canRedo", [](ScriptUiThemeEditor* self) { return self && self->editor().canRedo(); });
    themeEditor.addFunc("getRevision",
                        [](ScriptUiThemeEditor* self) { return self ? static_cast<int>(self->editor().revision()) : 0; });
    themeEditor.addFunc("getPreviewRevision", [](ScriptUiThemeEditor* self) {
        return self ? static_cast<int>(self->editor().previewRevision()) : 0;
    });
    themeEditor.addFunc("getSelectedId",
                        [](ScriptUiThemeEditor* self) { return self ? self->editor().selectedId() : std::string{}; });
    themeEditor.addFunc("getActiveId",
                        [](ScriptUiThemeEditor* self) { return self ? self->editor().activeId() : std::string{}; });
    themeEditor.addFunc("getPreviewRuntimeName", [](ScriptUiThemeEditor* self) {
        return self ? self->editor().previewRuntimeName() : std::string{};
    });
    themeEditor.addFunc("getThemeCount",
                        [](ScriptUiThemeEditor* self) { return self ? self->editor().themeCount() : 0; });
    themeEditor.addFunc("getThemeId", [](ScriptUiThemeEditor* self, int index) {
        return self ? self->editor().themeId(index) : std::string{};
    });
    themeEditor.addFunc("getThemeName", [](ScriptUiThemeEditor* self, int index) {
        return self ? self->editor().themeName(index) : std::string{};
    });
    themeEditor.addFunc("getThemeSelected",
                        [](ScriptUiThemeEditor* self, int index) { return self && self->editor().isThemeSelected(index); });
    themeEditor.addFunc("getThemeActive",
                        [](ScriptUiThemeEditor* self, int index) { return self && self->editor().isThemeActive(index); });
    themeEditor.addFunc("getColorR", [](ScriptUiThemeEditor* self, const std::string& path) {
        return self ? self->editor().getColorChannel(path, 0) : 0.0f;
    });
    themeEditor.addFunc("getColorG", [](ScriptUiThemeEditor* self, const std::string& path) {
        return self ? self->editor().getColorChannel(path, 1) : 0.0f;
    });
    themeEditor.addFunc("getColorB", [](ScriptUiThemeEditor* self, const std::string& path) {
        return self ? self->editor().getColorChannel(path, 2) : 0.0f;
    });
    themeEditor.addFunc("getColorA", [](ScriptUiThemeEditor* self, const std::string& path) {
        return self ? self->editor().getColorChannel(path, 3) : 1.0f;
    });
    themeEditor.addFunc("getFloat", [](ScriptUiThemeEditor* self, const std::string& path) {
        return self ? self->editor().getFloat(path) : 0.0f;
    });

    moduleClass.addFunc("create", [vm](UiEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "theme catalog id must not be empty",
                                  "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptUiThemeEditor>(
            vm, std::make_unique<ScriptUiThemeEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::ui_editor
