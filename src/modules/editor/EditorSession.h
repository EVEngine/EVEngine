#pragma once

#include "editor/EditorTool.h"

#include <string>
#include <vector>

namespace eve::editor {

/**
 * @brief Hosts interchangeable editor tools and routes their lifecycle/input.
 *
 * Tools are non-owning: the caller must keep a registered tool alive until it
 * is removed or the session is destroyed.
 */
class EditorSession {
public:
    EditorSession();
    ~EditorSession();

    EditorSession(const EditorSession &) = delete;
    EditorSession &operator=(const EditorSession &) = delete;

    /** @brief Register a tool. Returns false for null or duplicate id/pointer. */
    bool addTool(IEditorTool *tool);
    /** @brief Remove a registered tool by id, deactivating it when necessary. */
    bool removeTool(const std::string &id);
    /** @brief Remove all tools and release any pointer capture. */
    void clearTools();

    int getToolCount() const;
    IEditorTool *getTool(int index) const;
    IEditorTool *findTool(const std::string &id) const;

    /** @brief Activate a registered tool by id. Empty id deactivates all tools. */
    bool activateTool(const std::string &id);
    IEditorTool *activeTool() const { return activeTool_; }
    std::string activeToolId() const;

    /** @brief Route input to the active tool and apply pointer-capture response. */
    ToolResponse dispatchPointer(const EditorPointerEvent &event);
    /** @brief Route keyboard input to the active tool. */
    ToolResponse dispatchKey(const EditorKeyEvent &event);
    /** @brief Update the active tool. */
    void update(float dt);
    /** @brief Cancel its gesture and clear pointer capture. */
    void cancelActiveTool();

    bool hasPointerCapture() const { return capturedPointerId_ >= 0; }
    int capturedPointerId() const { return capturedPointerId_; }
    EditorContext &context() { return context_; }
    const EditorContext &context() const { return context_; }
    /** @brief Bind a non-owning editable target available to every tool callback. */
    void bindTarget(IEditableTarget *target) { context_.target_ = target; }
    IEditableTarget *target() const { return context_.target_; }

private:
    void deactivateCurrent();

    std::vector<IEditorTool *> tools_;
    IEditorTool *activeTool_ = nullptr;
    int capturedPointerId_ = -1;
    EditorContext context_;
};

}  // namespace eve::editor
