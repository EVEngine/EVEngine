#pragma once

#include "editor/EditorTarget.h"
#include "editor/EditorResult.h"

#include <memory>
#include <string>

namespace eve::editor {

class EditorSession;
class EditorTransactions;
class IEditCommand;
class IEditorOverlay;
class IEditorInspector;

/** @brief Pointer input normalized by an editor viewport adapter. */
struct EditorPointerEvent {
    enum class Phase { Down, Move, Up, Cancel };

    Phase phase     = Phase::Move;
    int   pointerId = 0;
    int   button    = 0;
    float x         = 0.f;
    float y         = 0.f;
    float z         = 0.f;
    float deltaX    = 0.f;
    float deltaY    = 0.f;
    float deltaZ    = 0.f;
    float pressure  = 1.f;
    bool  shift     = false;
    bool  control   = false;
    bool  alt       = false;
};

/** @brief Keyboard input normalized by the editor host. */
struct EditorKeyEvent {
    std::string key;
    bool        pressed  = false;
    bool        repeated = false;
    bool        shift    = false;
    bool        control  = false;
    bool        alt      = false;
};

/** @brief Result returned by a tool after processing input. */
struct ToolResponse {
    bool handled        = false;
    bool capturePointer = false;
    bool releasePointer = false;

    static ToolResponse ignored() { return {}; }
    static ToolResponse consumed() { return {true, false, false}; }
    static ToolResponse capture() { return {true, true, false}; }
    static ToolResponse release() { return {true, false, true}; }
};

/**
 * @brief Context supplied by EditorSession to tools.
 *
 * Later editor capabilities (targets, transactions, overlays, selection) attach
 * to this stable context instead of adding concrete tool cases to the session.
 */
class EditorContext {
public:
    explicit EditorContext(EditorSession* session = nullptr) : session_(session) {}

    /** @brief Session currently dispatching the tool callback. @return Borrowed session pointer, or null. @lifetime Valid only for the current dispatch callback. */
    EditorSession* session() const { return session_; }
    /** @brief Target currently bound to the session. @return Borrowed target pointer, or null. @lifetime Valid only for the current dispatch callback. */
    IEditableTarget* target() const;
    EditorTransactions& transactions() const;
    /** @brief Send a command through constraints into the active transaction. */
    bool execute(std::unique_ptr<IEditCommand> command) const;
    /** @brief Send a command without discarding validation or transaction diagnostics. */
    [[nodiscard]] EditorResult<void> executeChecked(std::unique_ptr<IEditCommand> command) const;

    /** @brief Query a capability from the current target. @return Borrowed capability pointer, or null. @lifetime Valid only for the current dispatch callback. */
    template <typename Capability>
    Capability* targetCapability() const;

private:
    friend class EditorSession;
    EditorSession* session_ = nullptr;
};

/** @brief UI-facing metadata supplied by a tool implementation. */
struct ToolDescriptor {
    std::string id;
    std::string label;
    std::string shortcut;
};

/**
 * @brief Protocol implemented by every editor tool.
 *
 * EditorSession only depends on this lifecycle. Tile, terrain, object and game-
 * specific tools are peers and can be added without modifying the session.
 */
class IEditorTool {
public:
    virtual ~IEditorTool() = default;

    /** @brief Stable tool identity and presentation metadata. */
    virtual const ToolDescriptor& descriptor() const = 0;

    /** @brief Called exactly once when this tool becomes active. */
    virtual void activate(EditorContext& context) { (void)context; }
    /** @brief Called exactly once before this tool stops being active. */
    virtual void deactivate(EditorContext& context) { (void)context; }

    /** @brief Handle normalized pointer input. */
    virtual ToolResponse pointerEvent(EditorContext& context, const EditorPointerEvent& event) {
        (void)context;
        (void)event;
        return ToolResponse::ignored();
    }

    /** @brief Handle normalized keyboard input. */
    virtual ToolResponse keyEvent(EditorContext& context, const EditorKeyEvent& event) {
        (void)context;
        (void)event;
        return ToolResponse::ignored();
    }

    /** @brief Per-frame hook for an active tool. */
    virtual void update(EditorContext& context, float dt) {
        (void)context;
        (void)dt;
    }

    /** @brief Emit renderer-independent feedback for the active viewport. */
    virtual void drawOverlay(EditorContext& context, IEditorOverlay& overlay) {
        (void)context;
        (void)overlay;
    }

    /** @brief Expose configurable settings through a host-provided inspector. */
    virtual void inspect(EditorContext& context, IEditorInspector& inspector) {
        (void)context;
        (void)inspector;
    }

    /** @brief Cancel the active gesture while keeping the tool selected. */
    virtual void cancel(EditorContext& context) { (void)context; }
};

}  // namespace eve::editor

namespace eve::editor {

template <typename Capability>
/** @return Borrowed capability pointer, or null. @lifetime Valid only for the current dispatch callback. */
Capability* EditorContext::targetCapability() const {
    IEditableTarget* current = target();
    return current ? current->query<Capability>() : nullptr;
}

}  // namespace eve::editor
