#include "editor/ActionTimelineWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::editor {
namespace {

constexpr float kHandleRadius = 6.0f;
constexpr float kNotifyWidth  = 8.0f;

EditorResult<void> widgetError(std::string rule, std::string message, EditorStatus status = EditorStatus::Rejected) {
    return EditorResult<void>::error(status, RuleId(std::move(rule)), std::move(message));
}

struct ItemView {
    LogicalId     trackId;
    LogicalId     itemId;
    LogicalId     type;
    Duration      start;
    Duration      end;
    Value::Object payload;
    bool          state  = false;
    bool          locked = false;
};

std::optional<ItemView> findItem(const action::ActionTimeline& timeline, const LogicalId& itemId) {
    for (const auto& track : timeline.tracks) {
        for (const auto& notify : track.notifies)
            if (notify.id == itemId)
                return ItemView{track.id,    notify.id,      notify.type, notify.time,
                                notify.time, notify.payload, false,       track.locked};
        for (const auto& state : track.states)
            if (state.id == itemId)
                return ItemView{track.id,  state.id,      state.type, state.start,
                                state.end, state.payload, true,       track.locked};
    }
    return std::nullopt;
}

Duration difference(Duration left, Duration right) {
    return Duration::fromNanoseconds(left.nanoseconds() - right.nanoseconds());
}

EditorResult<void> adapt(const EditorResult<std::size_t>& result, std::string rule, std::string message) {
    if (result.isAccepted()) return EditorResult<void>::applied();
    EditorResult<void> adapted;
    adapted.status      = result.status;
    adapted.diagnostics = result.diagnostics;
    if (adapted.diagnostics.empty())
        adapted.diagnostics.push_back({RuleId(std::move(rule)), DiagnosticSeverity::Error, std::move(message)});
    return adapted;
}

}  // namespace

ActionTimelineWidget::ActionTimelineWidget(ActionTimelineEditor& editor, const action::ActionNotifyRegistry& registry)
    : editor_(editor), registry_(registry) {}

EditorResult<void> ActionTimelineWidget::setViewport(float width, float rowHeight, float labelWidth) {
    if (!std::isfinite(width) || !std::isfinite(rowHeight) || !std::isfinite(labelWidth) || width <= 0.0f ||
        rowHeight < 12.0f || labelWidth < 0.0f || labelWidth + 16.0f >= width)
        return widgetError("editor.action.timeline.widget.viewport", "Timeline widget dimensions are invalid");
    width_      = width;
    rowHeight_  = rowHeight;
    labelWidth_ = labelWidth;
    return EditorResult<void>::applied();
}

float ActionTimelineWidget::timeToX(Duration time) const noexcept {
    const auto duration = editor_.target().timeline().duration.nanoseconds();
    if (duration <= 0) return labelWidth_;
    const double fraction = static_cast<double>(time.nanoseconds()) / static_cast<double>(duration);
    return labelWidth_ + static_cast<float>(fraction * static_cast<double>(width_ - labelWidth_));
}

Duration ActionTimelineWidget::xToTime(float x) const noexcept {
    const float clamped  = std::clamp(x, labelWidth_, width_);
    const float fraction = (clamped - labelWidth_) / (width_ - labelWidth_);
    const auto  duration = editor_.target().timeline().duration.nanoseconds();
    return Duration::fromNanoseconds(static_cast<std::int64_t>(std::llround(static_cast<double>(fraction) * duration)));
}

TimelineWidgetLayout ActionTimelineWidget::layout() const {
    TimelineWidgetLayout result;
    result.width          = width_;
    result.height         = rowHeight_ * static_cast<float>(editor_.target().timeline().tracks.size());
    result.playheadX      = timeToX(editor_.previewTime());
    const auto selected   = editor_.selectedItemIds();
    auto       isSelected = [&](const LogicalId& id) {
        return std::find(selected.begin(), selected.end(), id) != selected.end();
    };
    for (std::size_t row = 0; row < editor_.target().timeline().tracks.size(); ++row) {
        const auto& track = editor_.target().timeline().tracks[row];
        const float top   = static_cast<float>(row) * rowHeight_;
        for (const auto& notify : track.notifies) {
            Duration time = notify.time;
            if (drag_ && drag_->itemId == notify.id) time = drag_->previewStart;
            const float center = timeToX(time);
            result.items.push_back({track.id, notify.id, notify.type, false, isSelected(notify.id),
                                    center - kNotifyWidth * 0.5f, center + kNotifyWidth * 0.5f, top + 3.0f,
                                    top + rowHeight_ - 3.0f});
        }
        for (const auto& state : track.states) {
            Duration start = state.start;
            Duration end   = state.end;
            if (drag_ && drag_->itemId == state.id) {
                start = drag_->previewStart;
                end   = drag_->previewEnd;
            }
            const float minimum = timeToX(start);
            const float maximum = std::max(timeToX(end), minimum + 4.0f);
            result.items.push_back({track.id, state.id, state.type, true, isSelected(state.id), minimum, maximum,
                                    top + 3.0f, top + rowHeight_ - 3.0f});
        }
    }
    return result;
}

void ActionTimelineWidget::draw(IEditorOverlay& overlay) const {
    const auto& timeline = editor_.target().timeline();
    for (std::size_t row = 0; row < timeline.tracks.size(); ++row) {
        const float top    = static_cast<float>(row) * rowHeight_;
        const float bottom = top + rowHeight_;
        overlay.rectangle({0.0f, top, 0.0f}, {width_, bottom, 0.0f}, {0x20252cffU, 1.0f, true});
        overlay.line({0.0f, bottom, 0.0f}, {width_, bottom, 0.0f}, {0x4a5260ffU, 1.0f, false});
        overlay.text({4.0f, top + 4.0f, 0.0f}, timeline.tracks[row].label,
                     {timeline.tracks[row].muted ? 0x7f8792ffU : 0xd8dee9ffU, 1.0f, false});
    }
    for (const auto& item : layout().items) {
        const unsigned int color = item.selected ? 0xf2b84bffU : (item.state ? 0x568bd7ffU : 0x61c28bffU);
        overlay.rectangle({item.minimumX, item.minimumY, 0.0f}, {item.maximumX, item.maximumY, 0.0f},
                          {color, 1.0f, true});
        if (item.state) {
            overlay.line({item.minimumX, item.minimumY, 0.0f}, {item.minimumX, item.maximumY, 0.0f},
                         {0xffffffffU, 2.0f, false});
            overlay.line({item.maximumX, item.minimumY, 0.0f}, {item.maximumX, item.maximumY, 0.0f},
                         {0xffffffffU, 2.0f, false});
        }
    }
    const float height = rowHeight_ * static_cast<float>(timeline.tracks.size());
    overlay.line({timeToX(editor_.previewTime()), 0.0f, 0.0f}, {timeToX(editor_.previewTime()), height, 0.0f},
                 {0xff5b5bffU, 2.0f, false});
}

std::optional<TimelineHit> ActionTimelineWidget::hitTest(float x, float y) const {
    const auto projected = layout();
    for (auto it = projected.items.rbegin(); it != projected.items.rend(); ++it) {
        if (y < it->minimumY || y > it->maximumY || x < it->minimumX - kHandleRadius ||
            x > it->maximumX + kHandleRadius)
            continue;
        if (it->state && std::abs(x - it->minimumX) <= kHandleRadius)
            return TimelineHit{it->itemId, TimelineHitPart::StartHandle};
        if (it->state && std::abs(x - it->maximumX) <= kHandleRadius)
            return TimelineHit{it->itemId, TimelineHitPart::EndHandle};
        if (x >= it->minimumX && x <= it->maximumX) return TimelineHit{it->itemId, TimelineHitPart::Body};
    }
    return std::nullopt;
}

EditorResult<void> ActionTimelineWidget::pointerDown(float x, float y, bool additiveSelection) {
    if (drag_) return widgetError("editor.action.timeline.widget.drag-active", "A timeline drag is already active");
    const auto hit = hitTest(x, y);
    if (!hit) {
        if (!additiveSelection) editor_.clearSelection();
        return widgetError("editor.action.timeline.widget.hit-miss", "No timeline item was hit", EditorStatus::NoOp);
    }
    auto item = findItem(editor_.target().timeline(), hit->itemId);
    if (!item)
        return widgetError("editor.action.timeline.widget.item-missing", "Timeline item no longer exists",
                           EditorStatus::Conflict);
    if (item->locked) return widgetError("editor.action.timeline.track-locked", "Action track is locked");
    auto selected = editor_.selectItem(item->itemId, additiveSelection);
    if (!selected.isAccepted()) return selected;
    drag_ = DragState{item->itemId, hit->part, xToTime(x), item->start, item->end, item->start, item->end, item->state};
    return EditorResult<void>::applied();
}

EditorResult<void> ActionTimelineWidget::updateDrag(float x) {
    if (!drag_) return widgetError("editor.action.timeline.widget.drag-missing", "No timeline drag is active");
    const Duration cursor   = xToTime(x);
    const Duration duration = editor_.target().timeline().duration;
    const Duration delta    = difference(cursor, drag_->anchorTime);
    if (!drag_->state) {
        auto moved = drag_->originalStart.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "Notify drag overflowed");
        drag_->previewStart = std::clamp(moved.value(), Duration::zero(), duration);
        drag_->previewEnd   = drag_->previewStart;
        return EditorResult<void>::applied();
    }
    if (drag_->part == TimelineHitPart::StartHandle) {
        auto moved = drag_->originalStart.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State start drag overflowed");
        drag_->previewStart = std::clamp(moved.value(), Duration::zero(), drag_->originalEnd);
        drag_->previewEnd   = drag_->originalEnd;
        return EditorResult<void>::applied();
    }
    if (drag_->part == TimelineHitPart::EndHandle) {
        auto moved = drag_->originalEnd.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State end drag overflowed");
        drag_->previewStart = drag_->originalStart;
        drag_->previewEnd   = std::clamp(moved.value(), drag_->originalStart, duration);
        return EditorResult<void>::applied();
    }
    const auto span  = drag_->originalEnd.nanoseconds() - drag_->originalStart.nanoseconds();
    auto       moved = drag_->originalStart.tryAdd(delta);
    if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State drag overflowed");
    const auto latestStart = Duration::fromNanoseconds(duration.nanoseconds() - span);
    drag_->previewStart    = std::clamp(moved.value(), Duration::zero(), latestStart);
    drag_->previewEnd      = Duration::fromNanoseconds(drag_->previewStart.nanoseconds() + span);
    return EditorResult<void>::applied();
}

EditorResult<void> ActionTimelineWidget::pointerMove(float x) { return updateDrag(x); }

EditorResult<void> ActionTimelineWidget::pointerUp(float x) {
    auto updated = updateDrag(x);
    if (!updated.isAccepted()) return updated;
    const DragState completed = *drag_;
    drag_.reset();
    if (completed.state && completed.part != TimelineHitPart::Body)
        return editor_.resizeState(completed.itemId, completed.previewStart, completed.previewEnd);
    return editor_.moveItem(completed.itemId, difference(completed.previewStart, completed.originalStart));
}

EditorResult<void> ActionTimelineWidget::seek(float x) { return editor_.seek(xToTime(x)); }

EditorResult<void> ActionTimelineWidget::inspectSelection(IEditorInspector& inspector) {
    const auto selected = editor_.selectedItemIds();
    if (selected.size() != 1)
        return widgetError("editor.action.timeline.widget.inspect-selection",
                           "Inspector requires exactly one selected timeline item", EditorStatus::NoOp);
    auto item = findItem(editor_.target().timeline(), selected.front());
    if (!item)
        return widgetError("editor.action.timeline.widget.item-missing", "Selected item no longer exists",
                           EditorStatus::Conflict);

    float       startSeconds = static_cast<float>(item->start.seconds());
    float       endSeconds   = static_cast<float>(item->end.seconds());
    std::string type         = item->type.format();
    auto        payloadJson  = Value(item->payload).toJson();
    if (!payloadJson)
        return widgetError("editor.action.timeline.widget.payload-encode", "Could not encode item payload");
    std::string payload = payloadJson.value();

    inspector.beginGroup("action.timeline.item", item->state ? "Notify State" : "Notify");
    const float maximum       = static_cast<float>(editor_.target().timeline().duration.seconds());
    bool        timingChanged = inspector.scalar("start", item->state ? "Start" : "Time", startSeconds, 0.0f, maximum);
    if (item->state) timingChanged = inspector.scalar("end", "End", endSeconds, 0.0f, maximum) || timingChanged;
    const bool typeChanged    = inspector.string("type", "Type", type);
    const bool payloadChanged = inspector.string("payload", "Payload JSON", payload);
    inspector.endGroup();

    if (!timingChanged && !typeChanged && !payloadChanged) return EditorResult<void>::applied();
    Result<Duration> parsedStart =
        timingChanged ? Duration::fromSeconds(startSeconds) : Result<Duration>::success(item->start);
    Result<Duration> parsedEnd =
        timingChanged ? Duration::fromSeconds(endSeconds) : Result<Duration>::success(item->end);
    if (!parsedStart || !parsedEnd)
        return widgetError("editor.action.timeline.widget.time-invalid", "Inspector time is invalid");
    auto parsedType = LogicalId::parse(type);
    if (!parsedType) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    auto parsedPayload = Value::fromJson(payload);
    if (!parsedPayload || !parsedPayload.value().getIf<Value::Object>())
        return widgetError("editor.action.timeline.widget.payload-invalid", "Payload must be a JSON object");
    auto descriptor = registry_.descriptor(type);
    if (!descriptor)
        return widgetError("editor.action.timeline.widget.type-unregistered", "Notify type is not registered");
    const auto expected = item->state ? action::ActionNotifyShape::State : action::ActionNotifyShape::Instant;
    if (descriptor.value().shape != expected)
        return widgetError("editor.action.timeline.widget.shape-mismatch", "Notify type shape does not match item");
    const auto*                 object = parsedPayload.value().getIf<Value::Object>();
    action::ActionTimelineEvent event{
        item->state ? action::ActionTimelineEventKind::StateEnter : action::ActionTimelineEventKind::Notify,
        item->trackId,
        item->itemId,
        *parsedType,
        parsedStart.value(),
        *object};
    if (!registry_.validate(event))
        return widgetError("editor.action.timeline.widget.payload-contract", "Payload violates notify contract");
    return editor_.editItem(item->itemId, parsedStart.value(), item->state ? parsedEnd.value() : parsedStart.value(),
                            std::move(*parsedType), *object);
}

std::vector<TimelineWidgetCommandDescriptor> ActionTimelineWidget::commands() const {
    const bool selected = editor_.selectionCount() > 0;
    return {{TimelineWidgetCommand::Copy, "Copy", "Ctrl+C", selected},
            {TimelineWidgetCommand::Paste, "Paste at Playhead", "Ctrl+V", clipboardAnchor_.has_value()},
            {TimelineWidgetCommand::DeleteSelection, "Delete", "Delete", selected},
            {TimelineWidgetCommand::Undo, "Undo", "Ctrl+Z", editor_.canUndo()},
            {TimelineWidgetCommand::Redo, "Redo", "Ctrl+Y", editor_.canRedo()},
            {TimelineWidgetCommand::PlayPause, editor_.playing() ? "Pause" : "Play", "Space", true}};
}

EditorResult<void> ActionTimelineWidget::invoke(TimelineWidgetCommand command) {
    switch (command) {
        case TimelineWidgetCommand::Copy: {
            const auto selected = editor_.selectedItemIds();
            if (selected.empty())
                return widgetError("editor.action.timeline.selection-empty", "No timeline items are selected");
            std::optional<Duration> earliest;
            for (const auto& id : selected) {
                const auto item = findItem(editor_.target().timeline(), id);
                if (item && (!earliest || item->start < *earliest)) earliest = item->start;
            }
            auto copied = editor_.copySelection();
            if (copied.isAccepted()) clipboardAnchor_ = earliest;
            return adapt(copied, "editor.action.timeline.widget.copy", "Could not copy timeline selection");
        }
        case TimelineWidgetCommand::Paste: {
            if (!clipboardAnchor_)
                return widgetError("editor.action.timeline.clipboard-empty", "Action timeline clipboard is empty");
            return adapt(editor_.paste(difference(editor_.previewTime(), *clipboardAnchor_)),
                         "editor.action.timeline.widget.paste", "Could not paste timeline selection");
        }
        case TimelineWidgetCommand::DeleteSelection: return editor_.deleteSelection();
        case TimelineWidgetCommand::Undo: {
            auto result = editor_.undo();
            if (result.isAccepted()) return EditorResult<void>::applied();
            EditorResult<void> adapted;
            adapted.status      = result.status;
            adapted.diagnostics = result.diagnostics;
            return adapted;
        }
        case TimelineWidgetCommand::Redo: {
            auto result = editor_.redo();
            if (result.isAccepted()) return EditorResult<void>::applied();
            EditorResult<void> adapted;
            adapted.status      = result.status;
            adapted.diagnostics = result.diagnostics;
            return adapted;
        }
        case TimelineWidgetCommand::PlayPause:
            if (editor_.playing())
                editor_.pause();
            else
                editor_.play();
            return EditorResult<void>::applied();
    }
    return widgetError("editor.action.timeline.widget.command", "Timeline command is unsupported",
                       EditorStatus::Unsupported);
}

EditorResult<void> ActionTimelineWidget::handleShortcut(std::string_view shortcut) {
    if (shortcut == "Ctrl+C") return invoke(TimelineWidgetCommand::Copy);
    if (shortcut == "Ctrl+V") return invoke(TimelineWidgetCommand::Paste);
    if (shortcut == "Delete" || shortcut == "Backspace") return invoke(TimelineWidgetCommand::DeleteSelection);
    if (shortcut == "Ctrl+Z") return invoke(TimelineWidgetCommand::Undo);
    if (shortcut == "Ctrl+Y" || shortcut == "Ctrl+Shift+Z") return invoke(TimelineWidgetCommand::Redo);
    if (shortcut == "Space") return invoke(TimelineWidgetCommand::PlayPause);
    return widgetError("editor.action.timeline.widget.shortcut", "Timeline shortcut is unsupported",
                       EditorStatus::Unsupported);
}

std::vector<action::ActionNotifyDescriptor> ActionTimelineWidget::insertableTypes(
    action::ActionNotifyShape shape) const {
    auto descriptors = registry_.descriptors();
    std::erase_if(descriptors, [&](const auto& descriptor) { return descriptor.shape != shape; });
    return descriptors;
}

LogicalId ActionTimelineWidget::generatedItemId() {
    for (;;) {
        auto id = LogicalId::fromParts("editor", "timeline-item." + std::to_string(++generatedSequence_));
        if (!id) continue;
        if (!findItem(editor_.target().timeline(), *id)) return std::move(*id);
    }
}

EditorResult<void> ActionTimelineWidget::addNotifyAtCursor(const LogicalId& trackId, std::string_view type,
                                                           Value::Object payload) {
    auto parsed = LogicalId::parse(type);
    if (!parsed) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    action::ActionTimelineEvent event{
        action::ActionTimelineEventKind::Notify, trackId, generatedItemId(), *parsed, editor_.previewTime(), payload};
    auto valid = registry_.validate(event);
    if (!valid) return widgetError("editor.action.timeline.widget.notify-invalid", "Notify payload is invalid");
    return editor_.addNotify(trackId, {event.itemId, event.type, event.time, std::move(payload)});
}

EditorResult<void> ActionTimelineWidget::addStateAtCursor(const LogicalId& trackId, std::string_view type,
                                                          Duration duration, Value::Object payload) {
    if (duration < Duration::zero())
        return widgetError("editor.action.timeline.widget.state-duration", "Notify-state duration is negative");
    auto parsed = LogicalId::parse(type);
    if (!parsed) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    auto end = editor_.previewTime().tryAdd(duration);
    if (!end || end.value() > editor_.target().timeline().duration)
        return widgetError("editor.action.timeline.widget.state-range", "Notify-state exceeds the timeline");
    action::ActionTimelineEvent event{action::ActionTimelineEventKind::StateEnter,
                                      trackId,
                                      generatedItemId(),
                                      *parsed,
                                      editor_.previewTime(),
                                      payload};
    auto                        valid = registry_.validate(event);
    if (!valid) return widgetError("editor.action.timeline.widget.state-invalid", "Notify-state payload is invalid");
    return editor_.addState(trackId, {event.itemId, event.type, event.time, end.value(), std::move(payload)});
}

}  // namespace eve::editor
