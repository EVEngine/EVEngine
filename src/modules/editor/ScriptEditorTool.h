#pragma once

#include "editor/EditorTool.h"

#include <memory>

namespace ssq { class Object; }

namespace eve::editor {

/**
 * @brief IEditorTool adapter whose lifecycle and input callbacks are Squirrel closures.
 *
 * Pointer/key callbacks return bit flags: 1 handled, 2 capture pointer, 4 release
 * pointer. Missing callbacks behave like the default IEditorTool implementation.
 */
class ScriptEditorTool final : public IEditorTool {
public:
    ScriptEditorTool(std::string id, std::string label);
    ~ScriptEditorTool() override;
    ScriptEditorTool(const ScriptEditorTool &) = delete;
    ScriptEditorTool &operator=(const ScriptEditorTool &) = delete;

    const ToolDescriptor &descriptor() const override;
    /** @brief Set the shortcut shown by hosts. */
    void setShortcut(const std::string &shortcut);
    /** @brief Set a closure invoked when the tool becomes active. */
    void setActivateCallback(ssq::Object callback);
    /** @brief Set a closure invoked when the tool becomes inactive. */
    void setDeactivateCallback(ssq::Object callback);
    /** @brief Set a closure invoked for normalized pointer input. */
    void setPointerCallback(ssq::Object callback);
    /** @brief Set a closure invoked for normalized key input. */
    void setKeyCallback(ssq::Object callback);
    /** @brief Set a closure invoked every active frame with delta time. */
    void setUpdateCallback(ssq::Object callback);
    /** @brief Set a closure invoked when the current gesture is cancelled. */
    void setCancelCallback(ssq::Object callback);

    void activate(EditorContext &context) override;
    void deactivate(EditorContext &context) override;
    ToolResponse pointerEvent(EditorContext &context, const EditorPointerEvent &event) override;
    ToolResponse keyEvent(EditorContext &context, const EditorKeyEvent &event) override;
    void update(EditorContext &context, float dt) override;
    void cancel(EditorContext &context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
