#include "action/editor/ActionTimelinePayloadEditor.h"

#include "action/ActionVfxBlock.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace eve::editor {
namespace {

template <class T = void>
EditorResult<T> payloadError(std::string rule, std::string message) {
    return eve::editing::failed<T>(EditorStatus::Rejected, RuleId(std::move(rule)), std::move(message));
}

struct PayloadItem {
    LogicalId                 trackId;
    LogicalId                 type;
    action::ActionTimelineEventKind kind = action::ActionTimelineEventKind::Notify;
    Duration                  time;
    Duration                  end;
    Value::Object             payload;
};

std::optional<PayloadItem> findPayloadItem(const action::ActionTimeline& timeline, const LogicalId& itemId) {
    for (const auto& track : timeline.tracks) {
        const auto notify = std::find_if(track.notifies.begin(), track.notifies.end(),
                                         [&](const auto& value) { return value.id == itemId; });
        if (notify != track.notifies.end())
            return PayloadItem{track.id, notify->type, action::ActionTimelineEventKind::Notify, notify->time,
                               notify->time, notify->payload};
        const auto state = std::find_if(track.states.begin(), track.states.end(),
                                        [&](const auto& value) { return value.id == itemId; });
        if (state != track.states.end())
            return PayloadItem{track.id, state->type, action::ActionTimelineEventKind::StateEnter, state->start,
                               state->end, state->payload};
    }
    return std::nullopt;
}

}  // namespace

EditorResult<Value::Object> ActionTimelinePayloadEditor::payload(const LogicalId& itemId) const {
    const auto item = findPayloadItem(editor_.target().timeline(), itemId);
    if (!item)
        return payloadError<Value::Object>("editor.action.timeline.payload-item-missing",
                                           "Editable action-block payload was not found");
    return eve::editing::applied<Value::Object>(item->payload);
}

EditorResult<void> ActionTimelinePayloadEditor::patch(const LogicalId& itemId, Value::Object fields) {
    const auto item = findPayloadItem(editor_.target().timeline(), itemId);
    if (!item)
        return payloadError("editor.action.timeline.payload-item-missing",
                            "Editable action-block payload was not found");
    auto merged = item->payload;
    for (auto& [field, value] : fields) merged.insert_or_assign(std::move(field), std::move(value));
    action::ActionTimelineEvent event{item->kind, item->trackId, itemId, item->type, item->time, merged};
    auto valid = registry_.validate(event);
    if (!valid) return EditorResult<void>::failure(valid.status());
    return editor_.updateItem(itemId, item->type, std::move(merged));
}

EditorResult<void> ActionTimelinePayloadEditor::fitBlockToClip(const LogicalId& itemId) {
    const auto item = findPayloadItem(editor_.target().timeline(), itemId);
    if (!item)
        return payloadError("editor.action.timeline.payload-item-missing",
                            "Editable action-block payload was not found");
    if (item->kind != action::ActionTimelineEventKind::StateEnter ||
        item->type.format() != "presentation:vfx-state")
        return payloadError("editor.action.timeline.clip-fit-type", "Only VFX states can fit a block to a clip");
    auto binding = action::ActionVfxBinding::fromPayload(item->payload, action::ActionVfxShape::State);
    if (!binding) return EditorResult<void>::failure(binding.status());
    auto duration = Duration::fromSeconds(binding.value().clipEndTime - binding.value().clipStartTime);
    if (!duration) return EditorResult<void>::failure(duration.status());
    auto end = item->time.tryAdd(duration.value());
    if (!end)
        return payloadError("editor.action.timeline.clip-fit-overflow", "Fitted VFX block time overflowed");
    return editor_.editItem(itemId, item->time, end.value(), item->type, item->payload);
}

EditorResult<void> ActionTimelinePayloadEditor::fitClipToBlock(const LogicalId& itemId) {
    const auto item = findPayloadItem(editor_.target().timeline(), itemId);
    if (!item)
        return payloadError("editor.action.timeline.payload-item-missing",
                            "Editable action-block payload was not found");
    if (item->kind != action::ActionTimelineEventKind::StateEnter ||
        item->type.format() != "presentation:vfx-state")
        return payloadError("editor.action.timeline.clip-fit-type", "Only VFX states can fit a clip to a block");
    auto binding = action::ActionVfxBinding::fromPayload(item->payload, action::ActionVfxShape::State);
    if (!binding) return EditorResult<void>::failure(binding.status());
    const double blockDuration =
        Duration::fromNanoseconds(item->end.nanoseconds() - item->time.nanoseconds()).seconds();
    auto merged = item->payload;
    merged.insert_or_assign("clipEndTime", binding.value().clipStartTime + blockDuration);
    action::ActionTimelineEvent event{item->kind, item->trackId, itemId, item->type, item->time, merged};
    auto valid = registry_.validate(event);
    if (!valid) return EditorResult<void>::failure(valid.status());
    return editor_.editItem(itemId, item->time, item->end, item->type, std::move(merged));
}

}  // namespace eve::editor
