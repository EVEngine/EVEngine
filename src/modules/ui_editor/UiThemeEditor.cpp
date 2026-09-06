#include "ui_editor/UiThemeEditor.h"

#include "ui/UIHost.h"
#include "ui/UISystem.h"

#include <utility>

namespace eve::ui_editor {
namespace {

template <class T = void>
ui_editing::EditorResult<T> editorError(ui_editing::EditorStatus status, std::string rule, std::string message) {
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
    if (!previewed.ok())
        previewed.ignore("ui theme editor keeps an empty preview when the seeded catalog is rejected");
}

ui_editing::EditorResult<void> UiThemeEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
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
            !candidate.setPanelCapability(panel.id, "ui.theme") ||
            !candidate.setPanelContext(panel.id, panel.context))
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

ui_editing::EditorResult<void> UiThemeEditor::commit(
    ui_editing::EditorResult<ui_editing::DomainOperation> operation, std::string label) {
    if (!operation.ok())
        return ui_editing::EditorResult<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("ui.theme.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.ui-theme.begin", "Could not begin the theme transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending theme transaction already inactive");
        return ui_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok())
        return ui_editing::EditorResult<void>::failure(committed.status());
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::refreshPreview() {
    preview_ = previews_.build(target_, ui_editing::ObjectId(selectedId_), target_.revision());
    if (preview_.status != ui_editing::EditorStatus::Applied)
        return editorError(preview_.status, "editor.ui-theme.preview", "Could not rebuild the theme preview");
    return eve::editing::applied<void>();
}

ui_editing::EditorResult<void> UiThemeEditor::selectTheme(std::string id) {
    auto asset = target_.theme(ui_editing::ObjectId(id));
    if (!asset.ok()) return ui_editing::EditorResult<void>::failure(asset.status());
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::createFromPreset(std::string id, std::string name, std::string preset) {
    if (preset != "dark" && preset != "light")
        return editorError(ui_editing::EditorStatus::Rejected, "editor.ui-theme.preset-source",
                           "New themes must be created from dark or light");
    auto created = commit(target_.makeCreateFromPreset(ui_editing::ObjectId(id), name, presetFromName(preset)),
                          "Create theme");
    if (!created.ok()) return created;
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::duplicateSelected(std::string id, std::string name) {
    auto duplicated =
        commit(target_.makeDuplicate(ui_editing::ObjectId(selectedId_), ui_editing::ObjectId(id), std::move(name)),
               "Duplicate theme");
    if (!duplicated.ok()) return duplicated;
    selectedId_ = std::move(id);
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::deleteSelected() {
    auto deleted = commit(target_.makeDelete(ui_editing::ObjectId(selectedId_)), "Delete theme");
    if (!deleted.ok()) return deleted;
    selectedId_ = target_.activeId().value();
    if (selectedId_.empty() && !target_.themes().empty()) selectedId_ = target_.themes().front().id.value();
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::setActiveSelected() {
    auto activated = commit(target_.makeSetActive(ui_editing::ObjectId(selectedId_)), "Activate theme");
    if (!activated.ok()) return activated;
    auto published = publisher_.publish(target_);
    if (!published.ok()) return published;
    return refreshPreview();
}

ui_editing::EditorResult<void> UiThemeEditor::resetSelectedToBase() {
    return commit(target_.makeResetToBase(ui_editing::ObjectId(selectedId_)), "Reset theme");
}

ui_editing::EditorResult<void> UiThemeEditor::setToken(const std::string& path,
                                                       const ui_editing::EditorValue& value) {
    return commit(target_.makeSet(selection(), ui_editing::PropertyPath(path), value,
                                  ui_editing::PropertySetMode::Absolute),
                  "Set theme token");
}

ui_editing::EditorResult<editor::TransactionReceipt> UiThemeEditor::undo() {
    auto undone = transactions_.undo();
    if (!undone.ok()) return undone;
    if (!target_.theme(ui_editing::ObjectId(selectedId_)).ok() && !target_.themes().empty())
        selectedId_ = target_.themes().front().id.value();
    auto previewed = refreshPreview();
    if (!previewed.ok()) return ui_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return undone;
}

ui_editing::EditorResult<editor::TransactionReceipt> UiThemeEditor::redo() {
    auto redone = transactions_.redo();
    if (!redone.ok()) return redone;
    auto previewed = refreshPreview();
    if (!previewed.ok()) return ui_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return redone;
}

ui_editing::EditorResult<void> UiThemeEditor::applyPreviewHost(const std::string& hostName) {
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
