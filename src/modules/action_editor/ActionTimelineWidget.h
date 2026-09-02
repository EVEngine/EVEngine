#pragma once

/** @file ActionTimelineWidget.h @brief Renderer-neutral interactive action timeline widget. */

#include "action/ActionNotifyRegistry.h"
#include "action_editor/ActionTimelineEditor.h"
#include "editor/EditorPresentation.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::editor {

/** @brief Interactive portion of a projected timeline item. */
enum class TimelineHitPart : std::uint8_t { Body, StartHandle, EndHandle };

/** @brief Observable pointer-drag lifecycle state. */
enum class TimelineDragStatus : std::uint8_t { Idle, Active };

/** @brief Owning geometry projected for one timeline item. */
struct TimelineItemGeometry {
    LogicalId trackId;
    LogicalId itemId;
    LogicalId type;
    bool      state    = false;
    bool      selected = false;
    float     minimumX = 0.0f;
    float     maximumX = 0.0f;
    float     minimumY = 0.0f;
    float     maximumY = 0.0f;
};

/** @brief Result of widget hit testing. */
struct TimelineHit {
    LogicalId       itemId;
    TimelineHitPart part = TimelineHitPart::Body;
};

/** @brief Complete renderer-neutral layout for one widget frame. */
struct TimelineWidgetLayout {
    float                             width     = 0.0f;
    float                             height    = 0.0f;
    float                             playheadX = 0.0f;
    std::vector<TimelineItemGeometry> items;
};

/** @brief Standard context-menu and keyboard actions exposed by the widget. */
enum class TimelineWidgetCommand : std::uint8_t { Copy, Paste, DeleteSelection, Undo, Redo, PlayPause };

/** @brief One host-renderable command entry. */
struct TimelineWidgetCommandDescriptor {
    TimelineWidgetCommand command = TimelineWidgetCommand::Copy;
    std::string           label;
    std::string           shortcut;
    bool                  enabled = false;
};

/**
 * @brief Interactive timeline adapter shared by developer and in-game editor hosts.
 *
 * The widget borrows an editor and notify registry that must outlive it. Pointer
 * drags mutate only widget preview state until pointerUp commits exactly one
 * undoable domain transaction. All methods are owner-thread-only.
 */
class ActionTimelineWidget {
public:
    /** @brief Construct a widget over authoritative editor and notify services. */
    ActionTimelineWidget(ActionTimelineEditor& editor, const action::ActionNotifyRegistry& registry);

    /** @brief Configure host-space dimensions used for layout and hit testing. */
    [[nodiscard]] EditorResult<void> setViewport(float width, float rowHeight, float labelWidth = 120.0f);
    /** @brief Project the current authoritative timeline plus any active drag preview. */
    [[nodiscard]] TimelineWidgetLayout layout() const;
    /** @brief Emit rows, items, handles and playhead to an arbitrary overlay host. */
    void draw(IEditorOverlay& overlay) const;
    /** @brief Hit-test projected item bodies and state handles. */
    [[nodiscard]] std::optional<TimelineHit> hitTest(float x, float y) const;

    /** @brief Select an item and begin a pointer drag. */
    [[nodiscard]] EditorResult<void> pointerDown(float x, float y, bool additiveSelection = false);
    /** @brief Update the non-authoritative drag preview. */
    [[nodiscard]] EditorResult<void> pointerMove(float x);
    /** @brief Commit the active drag as one undoable edit. */
    [[nodiscard]] EditorResult<void> pointerUp(float x);
    /** @brief Cancel a drag without mutating the timeline. */
    void cancelPointer() noexcept { drag_.reset(); }
    /** @brief Current pointer-drag lifecycle state. */
    TimelineDragStatus dragStatus() const noexcept {
        return drag_ ? TimelineDragStatus::Active : TimelineDragStatus::Idle;
    }

    /** @brief Seek the editor preview cursor from a host-space coordinate. */
    [[nodiscard]] EditorResult<void> seek(float x);
    /** @brief Present and apply selected item timing, type and JSON payload fields. */
    [[nodiscard]] EditorResult<void> inspectSelection(IEditorInspector& inspector);

    /** @brief Return standard menu entries with current enabled state. */
    [[nodiscard]] std::vector<TimelineWidgetCommandDescriptor> commands() const;
    /** @brief Execute a standard menu command. */
    [[nodiscard]] EditorResult<void> invoke(TimelineWidgetCommand command);
    /** @brief Execute a normalized shortcut such as Ctrl+C, Delete or Space. */
    [[nodiscard]] EditorResult<void> handleShortcut(std::string_view shortcut);

    /** @brief Notify types suitable for an instant or state insertion picker. */
    [[nodiscard]] std::vector<action::ActionNotifyDescriptor> insertableTypes(action::ActionNotifyShape shape) const;
    /** @brief Insert a validated instant notify at the preview cursor. */
    [[nodiscard]] EditorResult<void> addNotifyAtCursor(const LogicalId& trackId, std::string_view type,
                                                       Value::Object payload);
    /** @brief Insert a validated notify state beginning at the preview cursor. */
    [[nodiscard]] EditorResult<void> addStateAtCursor(const LogicalId& trackId, std::string_view type,
                                                      Duration duration, Value::Object payload);

private:
    struct DragState {
        LogicalId       itemId;
        TimelineHitPart part = TimelineHitPart::Body;
        Duration        anchorTime;
        Duration        originalStart;
        Duration        originalEnd;
        Duration        previewStart;
        Duration        previewEnd;
        bool            state = false;
    };

    [[nodiscard]] float              timeToX(Duration time) const noexcept;
    [[nodiscard]] Duration           xToTime(float x) const noexcept;
    [[nodiscard]] EditorResult<void> updateDrag(float x);
    [[nodiscard]] LogicalId          generatedItemId();

    ActionTimelineEditor&               editor_;
    const action::ActionNotifyRegistry& registry_;
    float                               width_      = 800.0f;
    float                               rowHeight_  = 24.0f;
    float                               labelWidth_ = 120.0f;
    std::optional<DragState>            drag_;
    std::optional<Duration>             clipboardAnchor_;
    std::uint64_t                       generatedSequence_ = 0;
};

}  // namespace eve::editor
