#include "editor/EditorToolbar.h"

#include "common/Exception.h"

namespace eve::editor {

void EditorToolbar::clear() {
    tools_.clear();
    active_.clear();
}

int EditorToolbar::findIndex(const std::string &id) const {
    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (tools_[i].id == id) return i;
    }
    return -1;
}

void EditorToolbar::addTool(const std::string &id, const std::string &label) {
    if (id.empty()) throw Exception("EditorToolbar::addTool: empty id");
    if (findIndex(id) >= 0) throw Exception("EditorToolbar::addTool: duplicate id");
    tools_.push_back(Tool{id, label, ""});
    if (active_.empty()) active_ = id;
}

void EditorToolbar::setShortcut(const std::string &id, const std::string &key) {
    int i = findIndex(id);
    if (i < 0) throw Exception("EditorToolbar::setShortcut: unknown id");
    tools_[i].shortcut = key;
}

bool EditorToolbar::setActive(const std::string &id) {
    if (findIndex(id) < 0) return false;
    active_ = id;
    return true;
}

bool EditorToolbar::matchShortcut(const std::string &key) {
    for (const auto &t : tools_) {
        if (!t.shortcut.empty() && t.shortcut == key) {
            active_ = t.id;
            return true;
        }
    }
    return false;
}

std::string EditorToolbar::getToolId(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size()))
        throw Exception("EditorToolbar::getToolId: bad index");
    return tools_[index].id;
}

std::string EditorToolbar::getToolLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size()))
        throw Exception("EditorToolbar::getToolLabel: bad index");
    return tools_[index].label;
}

std::string EditorToolbar::getToolShortcut(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size()))
        throw Exception("EditorToolbar::getToolShortcut: bad index");
    return tools_[index].shortcut;
}

}  // namespace eve::editor
