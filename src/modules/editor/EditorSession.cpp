#include "editor/EditorSession.h"

#include <algorithm>

namespace eve::editor {

EditorSession::EditorSession() : context_(this) {}

EditorSession::~EditorSession() { deactivateCurrent(); }

bool EditorSession::addTool(IEditorTool *tool) {
    if (!tool || tool->descriptor().id.empty()) return false;
    for (auto *registered : tools_) {
        if (registered == tool || registered->descriptor().id == tool->descriptor().id) return false;
    }
    tools_.push_back(tool);
    return true;
}

bool EditorSession::removeTool(const std::string &id) {
    auto it = std::find_if(tools_.begin(), tools_.end(), [&](const IEditorTool *tool) {
        return tool && tool->descriptor().id == id;
    });
    if (it == tools_.end()) return false;
    if (*it == activeTool_) deactivateCurrent();
    tools_.erase(it);
    return true;
}

void EditorSession::clearTools() {
    deactivateCurrent();
    tools_.clear();
}

int EditorSession::getToolCount() const { return static_cast<int>(tools_.size()); }

IEditorTool *EditorSession::getTool(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size())) return nullptr;
    return tools_[static_cast<size_t>(index)];
}

IEditorTool *EditorSession::findTool(const std::string &id) const {
    for (auto *tool : tools_) {
        if (tool && tool->descriptor().id == id) return tool;
    }
    return nullptr;
}

bool EditorSession::activateTool(const std::string &id) {
    if (id.empty()) {
        deactivateCurrent();
        return true;
    }
    IEditorTool *next = findTool(id);
    if (!next) return false;
    if (next == activeTool_) return true;
    deactivateCurrent();
    activeTool_ = next;
    activeTool_->activate(context_);
    return true;
}

std::string EditorSession::activeToolId() const {
    return activeTool_ ? activeTool_->descriptor().id : std::string{};
}

ToolResponse EditorSession::dispatchPointer(const EditorPointerEvent &event) {
    if (!activeTool_) return ToolResponse::ignored();
    if (capturedPointerId_ >= 0 && event.pointerId != capturedPointerId_) {
        return ToolResponse::ignored();
    }
    ToolResponse response = activeTool_->pointerEvent(context_, event);
    if (response.capturePointer && capturedPointerId_ < 0) capturedPointerId_ = event.pointerId;
    if (response.releasePointer && capturedPointerId_ == event.pointerId) capturedPointerId_ = -1;
    if (event.phase == EditorPointerEvent::Phase::Cancel && capturedPointerId_ == event.pointerId) {
        capturedPointerId_ = -1;
    }
    return response;
}

ToolResponse EditorSession::dispatchKey(const EditorKeyEvent &event) {
    return activeTool_ ? activeTool_->keyEvent(context_, event) : ToolResponse::ignored();
}

void EditorSession::update(float dt) {
    if (activeTool_) activeTool_->update(context_, dt);
}

void EditorSession::cancelActiveTool() {
    if (activeTool_) activeTool_->cancel(context_);
    capturedPointerId_ = -1;
}

void EditorSession::deactivateCurrent() {
    if (activeTool_) {
        activeTool_->cancel(context_);
        activeTool_->deactivate(context_);
    }
    activeTool_ = nullptr;
    capturedPointerId_ = -1;
}

}  // namespace eve::editor
