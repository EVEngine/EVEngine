#include "climbing/ClimbingEventValue.h"

#include <array>
#include <string>
#include <utility>

namespace eve::climbing {
namespace {

struct EventKindName {
    ClimbingEventKind kind;
    const char*       name;
};

/**
 * 唯一的事件种类名表。`static_assert` 钉住长度，`test/climbing_events.cpp` 再逐个取值双向验证：
 * 加一个枚举值而忘了这里，测试会红（而不是让绑定与存档各写一个名字）。
 */
constexpr std::array<EventKindName, kClimbingEventKindCount> kEventKindNames = {{
    {ClimbingEventKind::Started, "started"},
    {ClimbingEventKind::AnchorTransitionStarted, "anchor_transition_started"},
    {ClimbingEventKind::AnchorReached, "anchor_reached"},
    {ClimbingEventKind::ContactLeftHand, "contact_left_hand"},
    {ClimbingEventKind::ContactRightHand, "contact_right_hand"},
    {ClimbingEventKind::Landed, "landed"},
    {ClimbingEventKind::Hanging, "hanging"},
    {ClimbingEventKind::Dropped, "dropped"},
    {ClimbingEventKind::Completed, "completed"},
    {ClimbingEventKind::Cancelled, "cancelled"},
    {ClimbingEventKind::Failed, "failed"},
}};

}  // namespace

std::string_view climbingEventKindName(ClimbingEventKind kind) {
    for (const EventKindName& entry : kEventKindNames) {
        if (entry.kind == kind) return entry.name;
    }
    return "failed";
}

EventKindParse parseClimbingEventKind(std::string_view name, ClimbingEventKind& out) {
    for (const EventKindName& entry : kEventKindNames) {
        if (name == entry.name) {
            out = entry.kind;
            return EventKindParse::Parsed;
        }
    }
    return EventKindParse::Unknown;
}

Value climbingEventValue(const ClimbingEvent& event) {
    Value::Array metadata;
    metadata.reserve(event.metadata.size());
    for (const std::string& value : event.metadata) metadata.emplace_back(value);
    return Value::object({
        {"kind", std::string(climbingEventKindName(event.kind))},
        {"actionId", event.actionId},
        {"tick", std::to_string(event.tick.value())},
        {"executionId", std::to_string(event.executionId.value())},
        {"metadata", Value(std::move(metadata))},
    });
}

Value climbingEventArrayValue(std::span<const ClimbingEvent> events) {
    Value::Array values;
    values.reserve(events.size());
    for (const ClimbingEvent& event : events) values.push_back(climbingEventValue(event));
    return Value(std::move(values));
}

}  // namespace eve::climbing
