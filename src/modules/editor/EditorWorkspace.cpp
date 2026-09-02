#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace eve::editor {
namespace {

std::optional<WorkspaceRegion> parseWorkspaceRegion(const std::string& region) {
    if (region == "left") return WorkspaceRegion::Left;
    if (region == "right") return WorkspaceRegion::Right;
    if (region == "top") return WorkspaceRegion::Top;
    if (region == "bottom") return WorkspaceRegion::Bottom;
    if (region == "center") return WorkspaceRegion::Center;
    if (region == "floating") return WorkspaceRegion::Floating;
    return std::nullopt;
}

template <class T>
EditorResult<T> workspaceError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

}  // namespace

std::string_view workspaceRegionName(WorkspaceRegion region) {
    switch (region) {
        case WorkspaceRegion::Left: return "left";
        case WorkspaceRegion::Right: return "right";
        case WorkspaceRegion::Top: return "top";
        case WorkspaceRegion::Bottom: return "bottom";
        case WorkspaceRegion::Center: return "center";
        case WorkspaceRegion::Floating: return "floating";
    }
    return "center";
}

EditorWorkspace::EditorWorkspace(std::string id, std::string title) : id_(std::move(id)), title_(std::move(title)) {}

void EditorWorkspace::setTitle(const std::string& title) {
    if (title_ == title) return;
    title_ = title;
    changed();
}

bool EditorWorkspace::registerPanel(const std::string& id, const std::string& title, const std::string& region,
                                    int order) {
    WorkspacePanelDescriptor descriptor{id, title, region, {}, {}, order, true, true};
    return registerPanel(std::move(descriptor)).ok();
}

EditorResult<WorkspacePanelDescriptor> EditorWorkspace::registerPanel(WorkspacePanelDescriptor descriptor) {
    if (descriptor.id.empty())
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::Rejected, "editor.workspace.empty-panel-id",
                                                         "Workspace panel id must not be empty");
    if (descriptor.title.empty())
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::Rejected, "editor.workspace.empty-panel-title",
                                                         "Workspace panel title must not be empty");
    if (!parseWorkspaceRegion(descriptor.region))
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::Rejected, "editor.workspace.invalid-region",
                                                         "Workspace panel uses an unsupported dock region");
    if (findPanel(descriptor.id))
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::Conflict, "editor.workspace.duplicate-panel",
                                                         "Workspace panel id is already registered: " + descriptor.id);
    const std::string id = descriptor.id;
    panels_.push_back(std::move(descriptor));
    sortPanels();
    if (activePanel_.empty()) activePanel_ = id;
    changed();
    return eve::editing::applied<WorkspacePanelDescriptor>(*findPanel(id));
}

bool EditorWorkspace::removePanel(const std::string& id) {
    auto result = removePanel(StableId(id));
    return result.code() == EditorStatus::Applied;
}

EditorResult<WorkspacePanelDescriptor> EditorWorkspace::removePanel(const StableId& id) {
    const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const auto& panel) { return panel.id == id.value(); });
    if (found == panels_.end())
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::NotFound, "editor.workspace.panel-not-found",
                                                         "Workspace panel is not registered: " + id.value());
    WorkspacePanelDescriptor removed = *found;
    panels_.erase(found);
    if (activePanel_ == id.value()) activePanel_ = panels_.empty() ? std::string{} : panels_.front().id;
    changed();
    return eve::editing::applied<WorkspacePanelDescriptor>(std::move(removed));
}

void EditorWorkspace::clearPanels() {
    if (panels_.empty()) return;
    panels_.clear();
    activePanel_.clear();
    changed();
}

bool EditorWorkspace::movePanel(const std::string& id, const std::string& region, int order) {
    const auto parsed = parseWorkspaceRegion(region);
    return parsed && movePanel(StableId(id), *parsed, order).ok();
}

EditorResult<WorkspacePanelDescriptor> EditorWorkspace::movePanel(const StableId& id, WorkspaceRegion region,
                                                                   int order) {
    WorkspacePanelDescriptor* panel = findPanel(id.value());
    if (!panel)
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::NotFound, "editor.workspace.panel-not-found",
                                                         "Workspace panel is not registered: " + id.value());
    const std::string regionName(workspaceRegionName(region));
    if (panel->region == regionName && panel->order == order) {
        return EditorResult<WorkspacePanelDescriptor>::success(
            *panel, eve::Status::success(EditorStatus::NoOp));
    }
    panel->region = regionName;
    panel->order  = order;
    sortPanels();
    changed();
    return eve::editing::applied<WorkspacePanelDescriptor>(*findPanel(id.value()));
}

EditorResult<WorkspacePanelDescriptor> EditorWorkspace::panelAt(std::size_t index) const {
    if (index >= panels_.size())
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::NotFound, "editor.workspace.panel-index",
                                                         "Workspace panel index is out of range");
    return eve::editing::applied<WorkspacePanelDescriptor>(panels_[index]);
}

bool EditorWorkspace::setPanelCapability(const std::string& id, const std::string& capability) {
    WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel) return false;
    if (panel->capability == capability) return true;
    panel->capability = capability;
    changed();
    return true;
}

bool EditorWorkspace::setPanelContext(const std::string& id, const std::string& context) {
    WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel) return false;
    if (panel->context == context) return true;
    panel->context = context;
    changed();
    return true;
}

bool EditorWorkspace::setPanelVisible(const std::string& id, bool visible) {
    WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel) return false;
    if (panel->visible == visible) return true;
    panel->visible = visible;
    if (!visible && activePanel_ == id) activePanel_.clear();
    changed();
    return true;
}

bool EditorWorkspace::setPanelSingleton(const std::string& id, bool singleton) {
    WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel) return false;
    if (panel->singleton == singleton) return true;
    panel->singleton = singleton;
    changed();
    return true;
}

bool EditorWorkspace::activatePanel(const std::string& id) {
    return activatePanel(StableId(id)).ok();
}

EditorResult<WorkspacePanelDescriptor> EditorWorkspace::activatePanel(const StableId& id) {
    const WorkspacePanelDescriptor* panel = findPanel(id.value());
    if (!panel)
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::NotFound, "editor.workspace.panel-not-found",
                                                         "Workspace panel is not registered: " + id.value());
    if (!panel->visible)
        return workspaceError<WorkspacePanelDescriptor>(EditorStatus::Rejected, "editor.workspace.panel-hidden",
                                                         "Hidden workspace panels cannot be activated");
    if (activePanel_ == id.value()) {
        return EditorResult<WorkspacePanelDescriptor>::success(
            *panel, eve::Status::success(EditorStatus::NoOp));
    }
    activePanel_ = id.value();
    changed();
    return eve::editing::applied<WorkspacePanelDescriptor>(*panel);
}

std::string EditorWorkspace::getPanelId(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->id : std::string{};
}

std::string EditorWorkspace::getPanelTitle(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->title : std::string{};
}

std::string EditorWorkspace::getPanelRegion(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->region : std::string{};
}

std::string EditorWorkspace::getPanelCapability(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->capability : std::string{};
}

std::string EditorWorkspace::getPanelContext(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->context : std::string{};
}

int EditorWorkspace::getPanelOrder(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel ? panel->order : 0;
}

bool EditorWorkspace::getPanelVisible(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel && panel->visible;
}

bool EditorWorkspace::getPanelSingleton(int index) const {
    const auto* panel = panelAtUnchecked(index);
    return panel && panel->singleton;
}

void EditorWorkspace::setRegionSize(const std::string& region, float pixels) {
    dock_.setRegionSize(region, pixels);
    changed();
}

void EditorWorkspace::layout(float width, float height) { dock_.layout(width, height); }

float EditorWorkspace::getRegionX(const std::string& region) const { return dock_.getRegionX(region); }
float EditorWorkspace::getRegionY(const std::string& region) const { return dock_.getRegionY(region); }
float EditorWorkspace::getRegionW(const std::string& region) const { return dock_.getRegionW(region); }
float EditorWorkspace::getRegionH(const std::string& region) const { return dock_.getRegionH(region); }

bool EditorWorkspace::setMode(const std::string& mode) {
    return setModeId(StableId(mode)).ok();
}

EditorResult<StableId> EditorWorkspace::setModeId(StableId mode) {
    if (mode.empty())
        return workspaceError<StableId>(EditorStatus::Rejected, "editor.workspace.empty-mode",
                                        "Workspace mode must not be empty");
    if (mode_ == mode.value()) {
        return EditorResult<StableId>::success(std::move(mode), eve::Status::success(EditorStatus::NoOp));
    }
    mode_ = mode.value();
    changed();
    return eve::editing::applied<StableId>(std::move(mode));
}

bool EditorWorkspace::select(const std::string& channel, const std::string& domain, const std::string& target,
                             const std::string& item, const std::string& type, bool additive) {
    SelectionDomain parsed;
    if (channel.empty() || target.empty() || item.empty() || !parseDomain(domain, parsed)) return false;
    return selectItem(channel, SelectionItem{parsed, TargetId(target), StableId(item), type}, additive).ok();
}

EditorResult<SelectionSnapshot> EditorWorkspace::selectItem(std::string channel, SelectionItem selected,
                                                             bool additive) {
    if (channel.empty() || selected.target.empty() || selected.item.empty())
        return workspaceError<SelectionSnapshot>(EditorStatus::Rejected, "editor.workspace.invalid-selection",
                                                  "Selection requires a channel, target and item");
    std::vector<SelectionItem> items;
    if (additive) items = selection_.snapshot(channel).items;
    if (std::find(items.begin(), items.end(), selected) == items.end()) items.push_back(selected);
    auto result = selection_.set(channel, std::move(items), selected);
    if (result.ok() && result.code() != EditorStatus::NoOp) changed();
    return result;
}

bool EditorWorkspace::clearSelection(const std::string& channel) {
    return clearSelectionChecked(channel).ok();
}

EditorResult<SelectionSnapshot> EditorWorkspace::clearSelectionChecked(const std::string& channel) {
    auto result = selection_.clear(channel);
    if (result.ok() && result.code() != EditorStatus::NoOp) changed();
    return result;
}

int EditorWorkspace::getSelectionCount(const std::string& channel) const {
    return static_cast<int>(selection_.snapshot(channel).items.size());
}

std::string EditorWorkspace::getSelectionItem(const std::string& channel, int index) const {
    const auto snapshot = selection_.snapshot(channel);
    return index >= 0 && index < static_cast<int>(snapshot.items.size())
               ? snapshot.items[static_cast<std::size_t>(index)].item.value()
               : std::string{};
}

std::string EditorWorkspace::getSelectionType(const std::string& channel, int index) const {
    const auto snapshot = selection_.snapshot(channel);
    return index >= 0 && index < static_cast<int>(snapshot.items.size())
               ? snapshot.items[static_cast<std::size_t>(index)].type
               : std::string{};
}

std::string EditorWorkspace::getPrimarySelection(const std::string& channel) const {
    const auto snapshot = selection_.snapshot(channel);
    return snapshot.primary ? snapshot.primary->item.value() : std::string{};
}

std::uint64_t EditorWorkspace::getSelectionSequence(const std::string& channel) const {
    return selection_.snapshot(channel).sequence;
}

bool EditorWorkspace::focus(const std::string& channel, const std::string& surface, const std::string& item) {
    return focusItem(channel, StableId(surface), StableId(item)).ok();
}

EditorResult<EditorFocusSnapshot> EditorWorkspace::focusItem(const std::string& channel, StableId surface,
                                                              StableId item) {
    auto result = focus_.focus(channel, std::move(surface), std::move(item));
    if (result.ok() && result.code() != EditorStatus::NoOp) changed();
    return result;
}

std::string EditorWorkspace::getFocusedSurface(const std::string& channel) const {
    return focus_.snapshot(channel).surface.value();
}

bool EditorWorkspace::isRegion(const std::string& region) {
    return region == "left" || region == "right" || region == "top" || region == "bottom" || region == "center" ||
           region == "floating";
}

bool EditorWorkspace::parseDomain(const std::string& value, SelectionDomain& domain) {
    if (value == "scene")
        domain = SelectionDomain::Scene;
    else if (value == "asset")
        domain = SelectionDomain::Asset;
    else if (value == "graph")
        domain = SelectionDomain::Graph;
    else if (value == "timeline")
        domain = SelectionDomain::Timeline;
    else if (value == "ui")
        domain = SelectionDomain::UI;
    else if (value == "custom")
        domain = SelectionDomain::Custom;
    else
        return false;
    return true;
}

WorkspacePanelDescriptor* EditorWorkspace::findPanel(const std::string& id) {
    const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const auto& panel) { return panel.id == id; });
    return found == panels_.end() ? nullptr : &*found;
}

const WorkspacePanelDescriptor* EditorWorkspace::panelAtUnchecked(int index) const {
    return index >= 0 && index < static_cast<int>(panels_.size()) ? &panels_[static_cast<std::size_t>(index)] : nullptr;
}

void EditorWorkspace::sortPanels() {
    std::stable_sort(panels_.begin(), panels_.end(), [](const auto& left, const auto& right) {
        if (left.region != right.region) return left.region < right.region;
        if (left.order != right.order) return left.order < right.order;
        return left.id < right.id;
    });
}

void EditorWorkspace::changed() { ++revision_; }

}  // namespace eve::editor
