#include "ui/editor/UiThemeEditorScriptBindings.h"

#include "editor/EditorWorkspace.h"
#include "ui/editor/UiEditorModule.h"
#include "ui/editor/UiThemeEditor.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::ui_editor {
namespace {

constexpr const char* kBindingSource = "editor.ui.theme.squirrel";

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
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto                     themeEditor = editor::addScriptClass<ScriptUiThemeEditor>(table, "UiThemeEditor");

    editor::registerEditorWorkspace<ScriptUiThemeEditor>(themeEditor, bind,
                                   "theme editor and workspace must not be null");
    themeEditor.addFunc("selectTheme", [bind](ScriptUiThemeEditor* self, const std::string& id) {
        return bind.checked(self, "theme editor must not be null", [&] { return self->editor().selectTheme(id); });
    });
    themeEditor.addFunc("createFromPreset", [bind](ScriptUiThemeEditor* self, const std::string& id,
                                                   const std::string& name, const std::string& preset) {
        return bind.checked(self, "theme editor must not be null",
                            [&] { return self->editor().createFromPreset(id, name, preset); });
    });
    themeEditor.addFunc("duplicateSelected",
                        [bind](ScriptUiThemeEditor* self, const std::string& id, const std::string& name) {
                            return bind.checked(self, "theme editor must not be null",
                                                [&] { return self->editor().duplicateSelected(id, name); });
                        });
    themeEditor.addFunc("deleteSelected", [bind](ScriptUiThemeEditor* self) {
        return bind.checked(self, "theme editor must not be null", [&] { return self->editor().deleteSelected(); });
    });
    themeEditor.addFunc("setActiveSelected", [bind](ScriptUiThemeEditor* self) {
        return bind.checked(self, "theme editor must not be null", [&] { return self->editor().setActiveSelected(); });
    });
    themeEditor.addFunc("resetSelectedToBase", [bind](ScriptUiThemeEditor* self) {
        return bind.checked(self, "theme editor must not be null",
                            [&] { return self->editor().resetSelectedToBase(); });
    });
    themeEditor.addFunc(
        "setColor", [bind](ScriptUiThemeEditor* self, const std::string& path, float r, float g, float b, float a) {
            return bind.checked(self, "theme editor must not be null", [&] {
                return self->editor().setToken(
                    path, ui_editing::EditorValue::Array{static_cast<double>(r), static_cast<double>(g),
                                                         static_cast<double>(b), static_cast<double>(a)});
            });
        });
    themeEditor.addFunc("setFloat", [bind](ScriptUiThemeEditor* self, const std::string& path, float value) {
        return bind.checked(self, "theme editor must not be null",
                            [&] { return self->editor().setToken(path, static_cast<double>(value)); });
    });
    themeEditor.addFunc("applyPreviewHost", [bind](ScriptUiThemeEditor* self, const std::string& hostName) {
        return bind.checked(self, "theme editor must not be null",
                            [&] { return self->editor().applyPreviewHost(hostName); });
    });
    editor::registerEditorHistory<ScriptUiThemeEditor>(themeEditor, bind, "theme editor must not be null");
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

    editor::registerEditorOwnedCreate<ScriptUiThemeEditor, UiEditorModule>(moduleClass, bind, "theme catalog id must not be empty");
}

}  // namespace eve::ui_editor
