#include "ui/editor/UiEditorModule.h"
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "common/SquirrelBindContext.h"
#include "common/Value.h"
#include "editor/EditorScriptProjection.h"
#include "editor/EditorWorkspace.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/editor/UiThemeEditor.h"
#include "ui/editor/UiThemeEditorScriptBindings.h"
#include "ui/editor/WorkspaceLayoutShellBindings.h"

// UiEditorModule.cpp
namespace eve::ui_editor {

Module_IMPL(UiEditorModule, new UiEditorModule());

UiEditorModule::UiEditorModule() = default;

void UiEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, UiEditorModule::create, false);
    exposeUiThemeEditorScriptBindings(table, module);
    exposeWorkspaceLayoutShellBindings(table, module);
}
void UiEditorModule::expose(ssq::Class&) {}

}  // namespace eve::ui_editor

// UiThemeEditor.cpp
namespace eve::ui_editor {
namespace {

template <class T = void>
ui_editing::Result<T> editorError(ui_editing::EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, ui_editing::RuleId(std::move(rule)), std::move(message));
}

ui_editing::UiThemeBasePreset presetFromName(const std::string& name) {
    if (name == "light") return ui_editing::UiThemeBasePreset::Light;
    return ui_editing::UiThemeBasePreset::Dark;
}

}  // namespace

UiThemeEditor::UiThemeEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    auto previewed = refreshPreview();
    if (!previewed.ok()) previewed.ignore("ui theme editor keeps an empty preview when the seeded catalog is rejected");
}

ui_editing::Result<void> UiThemeEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"ui.themes", "Themes", "left", "list", 100},
        {"ui.preview", "Theme Preview", "center", "preview", 100},
        {"ui.inspector", "Theme Inspector", "right", "inspector", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "ui.theme") || !candidate.setPanelContext(panel.id, panel.context))
            return editorError(ui_editing::EditorStatus::Rejected, "editor.ui-theme.workspace-conflict",
                               "Could not install the UI theme workspace composition");
    }
    if (!candidate.activatePanel("ui.preview"))
        return editorError(ui_editing::EditorStatus::Rejected, "editor.ui-theme.workspace-activate",
                           "Could not activate the UI theme preview panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

editor::SelectionSnapshot UiThemeEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "ui-theme";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(selectedId_);
    item.type   = "ui.theme";
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

ui_editing::Result<void> UiThemeEditor::commit(ui_editing::Result<ui_editing::DomainOperation> operation,
                                               std::string                                     label) {
    if (!operation.ok()) return ui_editing::Result<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("ui.theme.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok()) return editorError(begun.code(), "editor.ui-theme.begin", "Could not begin the theme transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending theme transaction already inactive");
        return ui_editing::Result<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return ui_editing::Result<void>::failure(committed.status());
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::refreshPreview() {
    preview_ = previews_.build(target_, ui_editing::ObjectId(selectedId_), target_.revision());
    if (preview_.status != ui_editing::EditorStatus::Applied)
        return editorError(preview_.status, "editor.ui-theme.preview", "Could not rebuild the theme preview");
    return eve::editing::applied<void>();
}

ui_editing::Result<void> UiThemeEditor::selectTheme(std::string id) {
    auto asset = target_.theme(ui_editing::ObjectId(id));
    if (!asset.ok()) return ui_editing::Result<void>::failure(asset.status());
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::createFromPreset(std::string id, std::string name, std::string preset) {
    if (preset != "dark" && preset != "light")
        return editorError(ui_editing::EditorStatus::Rejected, "editor.ui-theme.preset-source",
                           "New themes must be created from dark or light");
    auto created =
        commit(target_.makeCreateFromPreset(ui_editing::ObjectId(id), name, presetFromName(preset)), "Create theme");
    if (!created.ok()) return created;
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::duplicateSelected(std::string id, std::string name) {
    auto duplicated =
        commit(target_.makeDuplicate(ui_editing::ObjectId(selectedId_), ui_editing::ObjectId(id), std::move(name)),
               "Duplicate theme");
    if (!duplicated.ok()) return duplicated;
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::deleteSelected() {
    auto deleted = commit(target_.makeDelete(ui_editing::ObjectId(selectedId_)), "Delete theme");
    if (!deleted.ok()) return deleted;
    selectedId_ = target_.activeId().value();
    if (selectedId_.empty() && !target_.themes().empty()) selectedId_ = target_.themes().front().id.value();
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::setActiveSelected() {
    auto activated = commit(target_.makeSetActive(ui_editing::ObjectId(selectedId_)), "Activate theme");
    if (!activated.ok()) return activated;
    auto published = publisher_.publish(target_);
    if (!published.ok()) return published;
    return refreshPreview();
}

ui_editing::Result<void> UiThemeEditor::resetSelectedToBase() {
    return commit(target_.makeResetToBase(ui_editing::ObjectId(selectedId_)), "Reset theme");
}

ui_editing::Result<void> UiThemeEditor::setToken(const std::string& path, const ui_editing::EditorValue& value) {
    return commit(
        target_.makeSet(selection(), ui_editing::PropertyPath(path), value, ui_editing::PropertySetMode::Absolute),
        "Set theme token");
}

ui_editing::Result<editor::TransactionReceipt> UiThemeEditor::undo() {
    auto undone = transactions_.undo();
    if (!undone.ok()) return undone;
    if (!target_.theme(ui_editing::ObjectId(selectedId_)).ok() && !target_.themes().empty())
        selectedId_ = target_.themes().front().id.value();
    auto previewed = refreshPreview();
    if (!previewed.ok()) return ui_editing::Result<editor::TransactionReceipt>::failure(previewed.status());
    return undone;
}

ui_editing::Result<editor::TransactionReceipt> UiThemeEditor::redo() {
    auto redone = transactions_.redo();
    if (!redone.ok()) return redone;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return ui_editing::Result<editor::TransactionReceipt>::failure(previewed.status());
    return redone;
}

ui_editing::Result<void> UiThemeEditor::applyPreviewHost(const std::string& hostName) {
    if (hostName.empty())
        return editorError(ui_editing::EditorStatus::Rejected, "editor.ui-theme.host",
                           "Preview host name must not be empty");
    if (preview_.status != ui_editing::EditorStatus::Applied || preview_.documentRevision != target_.revision()) {
        auto previewed = refreshPreview();
        if (!previewed.ok()) return previewed;
    }
    auto host = ui::UIHost::resolve(ui::UISystem::findHost(hostName));
    if (!host)
        return editorError(ui_editing::EditorStatus::NotFound, "editor.ui-theme.host-missing",
                           "Preview UI host was not found: " + hostName);
    host->get().setThemeOverride(preview_.theme);
    return eve::editing::applied<void>();
}

const ui_editing::UiThemeAsset* UiThemeEditor::themeAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.themes().size()) return nullptr;
    return &target_.themes()[static_cast<std::size_t>(index)];
}

std::string UiThemeEditor::themeId(int index) const {
    const auto* asset = themeAt(index);
    return asset ? asset->id.value() : std::string{};
}
std::string UiThemeEditor::themeName(int index) const {
    const auto* asset = themeAt(index);
    return asset ? asset->name : std::string{};
}
bool UiThemeEditor::isThemeSelected(int index) const {
    const auto* asset = themeAt(index);
    return asset && asset->id.value() == selectedId_;
}
bool UiThemeEditor::isThemeActive(int index) const {
    const auto* asset = themeAt(index);
    return asset && asset->id == target_.activeId();
}

float UiThemeEditor::getColorChannel(const std::string& path, int channel) const {
    auto value = target_.read(selection(), ui_editing::PropertyPath(path));
    if (value.state != ui_editing::PropertyReadState::Value) return 0.0f;
    const auto* array = value.value.getIf<ui_editing::EditorValue::Array>();
    if (!array || channel < 0 || static_cast<std::size_t>(channel) >= array->size()) return 0.0f;
    const auto* number = (*array)[static_cast<std::size_t>(channel)].getIf<double>();
    return number ? static_cast<float>(*number) : 0.0f;
}

float UiThemeEditor::getFloat(const std::string& path) const {
    auto value = target_.read(selection(), ui_editing::PropertyPath(path));
    if (value.state != ui_editing::PropertyReadState::Value) return 0.0f;
    const auto* number = value.value.getIf<double>();
    return number ? static_cast<float>(*number) : 0.0f;
}

}  // namespace eve::ui_editor

// UiThemeEditorScriptBindings.cpp
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
    script::bindNullSafe<ScriptUiThemeEditor>(
        themeEditor, "getPreviewRevision",
        [](ScriptUiThemeEditor& self) { return static_cast<int>(self.editor().previewRevision()); }, 0);
    script::bindNullSafe<ScriptUiThemeEditor>(
        themeEditor, "getSelectedId", [](ScriptUiThemeEditor& self) { return self.editor().selectedId(); },
        std::string{});
    script::bindNullSafe<ScriptUiThemeEditor>(
        themeEditor, "getActiveId", [](ScriptUiThemeEditor& self) { return self.editor().activeId(); }, std::string{});
    script::bindNullSafe<ScriptUiThemeEditor>(
        themeEditor, "getPreviewRuntimeName",
        [](ScriptUiThemeEditor& self) { return self.editor().previewRuntimeName(); }, std::string{});
    script::bindNullSafe<ScriptUiThemeEditor>(
        themeEditor, "getThemeCount", [](ScriptUiThemeEditor& self) { return self.editor().themeCount(); }, 0);
    themeEditor.addFunc("getThemeId", [](ScriptUiThemeEditor* self, int index) {
        return self ? self->editor().themeId(index) : std::string{};
    });
    themeEditor.addFunc("getThemeName", [](ScriptUiThemeEditor* self, int index) {
        return self ? self->editor().themeName(index) : std::string{};
    });
    themeEditor.addFunc("getThemeSelected", [](ScriptUiThemeEditor* self, int index) {
        return self && self->editor().isThemeSelected(index);
    });
    themeEditor.addFunc("getThemeActive", [](ScriptUiThemeEditor* self, int index) {
        return self && self->editor().isThemeActive(index);
    });
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
    // Additive structured snapshot; atomic getters above remain the compatibility surface.
    themeEditor.addFunc("getState", [vm = table.getHandle()](ScriptUiThemeEditor* self) {
        if (!self) {
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "theme editor must not be null",
                                                      {}, {}, kBindingSource)));
        }
        const auto& editor = self->editor();
        return script::projectStatusResult(
            vm, Status::success(StatusCode::Applied),
            Value(Value::Object{
                {"selectedId", Value(editor.selectedId())},
                {"activeId", Value(editor.activeId())},
                {"previewRuntimeName", Value(editor.previewRuntimeName())},
                {"revision", Value(static_cast<std::int64_t>(editor.revision()))},
                {"previewRevision", Value(static_cast<std::int64_t>(editor.previewRevision()))},
                {"themeCount", Value(editor.themeCount())},
            }));
    });

    editor::registerEditorOwnedCreate<ScriptUiThemeEditor, UiEditorModule>(moduleClass, bind,
                                                                           "theme catalog id must not be empty");
}

}  // namespace eve::ui_editor
