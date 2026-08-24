#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <utility>

namespace eve::editor {

EditorWorkspace::EditorWorkspace(std::string id, std::string title) : id_(std::move(id)), title_(std::move(title)) {}

void EditorWorkspace::setTitle(const std::string& title) {
    if (title_ == title) return;
    title_ = title;
    changed();
}

bool EditorWorkspace::registerPanel(const std::string& id, const std::string& title, const std::string& region,
                                    int order) {
    if (id.empty() || title.empty() || !isRegion(region) || findPanel(id)) return false;
    panels_.push_back({id, title, region, {}, {}, order, true, true});
    sortPanels();
    if (activePanel_.empty()) activePanel_ = id;
    changed();
    return true;
}

bool EditorWorkspace::removePanel(const std::string& id) {
    const auto found = std::find_if(panels_.begin(), panels_.end(), [&](const auto& panel) { return panel.id == id; });
    if (found == panels_.end()) return false;
    panels_.erase(found);
    if (activePanel_ == id) activePanel_ = panels_.empty() ? std::string{} : panels_.front().id;
    changed();
    return true;
}

void EditorWorkspace::clearPanels() {
    if (panels_.empty()) return;
    panels_.clear();
    activePanel_.clear();
    changed();
}

bool EditorWorkspace::movePanel(const std::string& id, const std::string& region, int order) {
    WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel || !isRegion(region)) return false;
    if (panel->region == region && panel->order == order) return true;
    panel->region = region;
    panel->order  = order;
    sortPanels();
    changed();
    return true;
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
    const WorkspacePanelDescriptor* panel = findPanel(id);
    if (!panel || !panel->visible) return false;
    if (activePanel_ == id) return true;
    activePanel_ = id;
    changed();
    return true;
}

std::string EditorWorkspace::getPanelId(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->id : std::string{};
}

std::string EditorWorkspace::getPanelTitle(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->title : std::string{};
}

std::string EditorWorkspace::getPanelRegion(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->region : std::string{};
}

std::string EditorWorkspace::getPanelCapability(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->capability : std::string{};
}

std::string EditorWorkspace::getPanelContext(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->context : std::string{};
}

int EditorWorkspace::getPanelOrder(int index) const {
    const auto* panel = panelAt(index);
    return panel ? panel->order : 0;
}

bool EditorWorkspace::getPanelVisible(int index) const {
    const auto* panel = panelAt(index);
    return panel && panel->visible;
}

bool EditorWorkspace::getPanelSingleton(int index) const {
    const auto* panel = panelAt(index);
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
    if (mode.empty()) return false;
    if (mode_ == mode) return true;
    mode_ = mode;
    changed();
    return true;
}

bool EditorWorkspace::select(const std::string& channel, const std::string& domain, const std::string& target,
                             const std::string& item, const std::string& type, bool additive) {
    SelectionDomain parsed;
    if (channel.empty() || target.empty() || item.empty() || !parseDomain(domain, parsed)) return false;
    SelectionItem              selected{parsed, TargetId(target), StableId(item), type};
    std::vector<SelectionItem> items;
    if (additive) items = selection_.snapshot(channel).items;
    if (std::find(items.begin(), items.end(), selected) == items.end()) items.push_back(selected);
    const auto result = selection_.set(channel, std::move(items), selected);
    if (!result.accepted()) return false;
    changed();
    return true;
}

bool EditorWorkspace::clearSelection(const std::string& channel) {
    const auto result = selection_.clear(channel);
    if (!result.accepted()) return false;
    changed();
    return true;
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
    return focus_.focus(channel, StableId(surface), StableId(item)).accepted();
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

const WorkspacePanelDescriptor* EditorWorkspace::panelAt(int index) const {
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
