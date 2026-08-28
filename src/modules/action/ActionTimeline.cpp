#include "action/ActionTimeline.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace eve::action {
namespace {

template <class T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

const Value* field(const Value& value, std::string_view name) { return value.find(std::string(name)); }

Result<std::string> stringField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isString()) return invalid<std::string>("expected string field", std::move(path));
    return Result<std::string>::success(member->asString());
}

Result<std::int64_t> intField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isInt64()) return invalid<std::int64_t>("expected integer field", std::move(path));
    return Result<std::int64_t>::success(member->asInt());
}

Result<bool> boolField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isBool()) return invalid<bool>("expected boolean field", std::move(path));
    return Result<bool>::success(member->asBool());
}

Result<LogicalId> logicalIdField(const Value& value, std::string_view name, std::string path) {
    auto text = stringField(value, name, path);
    if (!text) return Result<LogicalId>::failure(text.status());
    auto parsed = LogicalId::parse(text.value());
    if (!parsed) return invalid<LogicalId>("expected namespace:name logical id", std::move(path));
    return Result<LogicalId>::success(std::move(*parsed));
}

Value payloadValue(const Value::Object& payload) { return Value(payload); }

Result<Value::Object> payloadField(const Value& value, std::string path) {
    const Value* payload = field(value, "payload");
    if (!payload || !payload->isObject()) return invalid<Value::Object>("expected object payload", std::move(path));
    return Result<Value::Object>::success(*payload->getIf<Value::Object>());
}

Value encodeNotify(const ActionNotify& notify) {
    Value::Object object;
    object["id"]      = notify.id.format();
    object["type"]    = notify.type.format();
    object["timeNs"]  = notify.time.nanoseconds();
    object["payload"] = payloadValue(notify.payload);
    return Value(std::move(object));
}

Value encodeState(const ActionNotifyState& state) {
    Value::Object object;
    object["id"]      = state.id.format();
    object["type"]    = state.type.format();
    object["startNs"] = state.start.nanoseconds();
    object["endNs"]   = state.end.nanoseconds();
    object["payload"] = payloadValue(state.payload);
    return Value(std::move(object));
}

Result<ActionNotify> decodeNotify(const Value& value, const std::string& path) {
    if (!value.isObject()) return invalid<ActionNotify>("expected notify object", path);
    auto id = logicalIdField(value, "id", path + ".id");
    if (!id) return Result<ActionNotify>::failure(id.status());
    auto type = logicalIdField(value, "type", path + ".type");
    if (!type) return Result<ActionNotify>::failure(type.status());
    auto time = intField(value, "timeNs", path + ".timeNs");
    if (!time) return Result<ActionNotify>::failure(time.status());
    auto payload = payloadField(value, path + ".payload");
    if (!payload) return Result<ActionNotify>::failure(payload.status());
    return Result<ActionNotify>::success(ActionNotify{std::move(id).takeValue(), std::move(type).takeValue(),
                                                      Duration::fromNanoseconds(time.value()),
                                                      std::move(payload).takeValue()});
}

Result<ActionNotifyState> decodeState(const Value& value, const std::string& path) {
    if (!value.isObject()) return invalid<ActionNotifyState>("expected notify-state object", path);
    auto id = logicalIdField(value, "id", path + ".id");
    if (!id) return Result<ActionNotifyState>::failure(id.status());
    auto type = logicalIdField(value, "type", path + ".type");
    if (!type) return Result<ActionNotifyState>::failure(type.status());
    auto start = intField(value, "startNs", path + ".startNs");
    if (!start) return Result<ActionNotifyState>::failure(start.status());
    auto end = intField(value, "endNs", path + ".endNs");
    if (!end) return Result<ActionNotifyState>::failure(end.status());
    auto payload = payloadField(value, path + ".payload");
    if (!payload) return Result<ActionNotifyState>::failure(payload.status());
    return Result<ActionNotifyState>::success(ActionNotifyState{
        std::move(id).takeValue(), std::move(type).takeValue(), Duration::fromNanoseconds(start.value()),
        Duration::fromNanoseconds(end.value()), std::move(payload).takeValue()});
}

bool inWindow(Duration value, Duration previous, Duration current, bool includePrevious) {
    return includePrevious ? value >= previous && value <= current : value > previous && value <= current;
}

}  // namespace

std::string_view actionTrackKindName(ActionTrackKind kind) noexcept {
    switch (kind) {
        case ActionTrackKind::Animation: return "animation";
        case ActionTrackKind::Gameplay: return "gameplay";
        case ActionTrackKind::Effect: return "effect";
        case ActionTrackKind::Audio: return "audio";
        case ActionTrackKind::Camera: return "camera";
        case ActionTrackKind::Movement: return "movement";
        case ActionTrackKind::Tag: return "tag";
        case ActionTrackKind::Custom: return "custom";
    }
    return "custom";
}

Result<ActionTrackKind> parseActionTrackKind(std::string_view text) {
    for (ActionTrackKind kind :
         {ActionTrackKind::Animation, ActionTrackKind::Gameplay, ActionTrackKind::Effect, ActionTrackKind::Audio,
          ActionTrackKind::Camera, ActionTrackKind::Movement, ActionTrackKind::Tag, ActionTrackKind::Custom})
        if (actionTrackKindName(kind) == text) return Result<ActionTrackKind>::success(kind);
    return invalid<ActionTrackKind>("unknown action track kind", "tracks.kind");
}

Result<void> ActionTimeline::validate() const {
    if (schemaVersion.value() != kActionTimelineSchemaVersion)
        return invalid("unsupported action timeline schema version", "schemaVersion");
    if (!actionId.isValid()) return invalid("action timeline requires a valid action id", "actionId");
    if (duration.nanoseconds() < 0) return invalid("timeline duration must be non-negative", "durationNs");

    std::set<std::string> ids;
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const auto&       track = tracks[trackIndex];
        const std::string path  = "tracks[" + std::to_string(trackIndex) + "]";
        if (!track.id.isValid()) return invalid("track id is invalid", path + ".id");
        if (!ids.insert(track.id.format()).second) return invalid("timeline item ids must be unique", path + ".id");
        for (std::size_t index = 0; index < track.notifies.size(); ++index) {
            const auto&       notify   = track.notifies[index];
            const std::string itemPath = path + ".notifies[" + std::to_string(index) + "]";
            if (!notify.id.isValid() || !notify.type.isValid()) return invalid("notify ids are invalid", itemPath);
            if (!ids.insert(notify.id.format()).second)
                return invalid("timeline item ids must be unique", itemPath + ".id");
            if (notify.time < Duration::zero() || notify.time > duration)
                return invalid("notify time is outside the timeline", itemPath + ".timeNs");
            if (index > 0 && track.notifies[index - 1].time > notify.time)
                return invalid("notifies must be sorted by time", itemPath + ".timeNs");
        }
        for (std::size_t index = 0; index < track.states.size(); ++index) {
            const auto&       state    = track.states[index];
            const std::string itemPath = path + ".states[" + std::to_string(index) + "]";
            if (!state.id.isValid() || !state.type.isValid()) return invalid("notify-state ids are invalid", itemPath);
            if (!ids.insert(state.id.format()).second)
                return invalid("timeline item ids must be unique", itemPath + ".id");
            if (state.start < Duration::zero() || state.end <= state.start || state.end > duration)
                return invalid("notify-state range is invalid", itemPath);
            if (index > 0 && track.states[index - 1].start > state.start)
                return invalid("notify states must be sorted by start time", itemPath + ".startNs");
        }
    }
    return Result<void>::success();
}

Result<std::vector<ActionTimelineEvent>> ActionTimeline::sample(Duration previous, Duration current,
                                                                bool includePrevious) const {
    auto valid = validate();
    if (!valid) return Result<std::vector<ActionTimelineEvent>>::failure(valid.status());
    if (previous < Duration::zero() || current < previous || current > duration)
        return invalid<std::vector<ActionTimelineEvent>>("sample range is outside the timeline", "sample");

    std::vector<ActionTimelineEvent> out;
    for (const auto& track : tracks) {
        if (track.muted) continue;
        for (const auto& notify : track.notifies)
            if (inWindow(notify.time, previous, current, includePrevious))
                out.push_back(
                    {ActionTimelineEventKind::Notify, track.id, notify.id, notify.type, notify.time, notify.payload});
        for (const auto& state : track.states) {
            if (inWindow(state.start, previous, current, includePrevious))
                out.push_back(
                    {ActionTimelineEventKind::StateEnter, track.id, state.id, state.type, state.start, state.payload});
            if (inWindow(state.end, previous, current, includePrevious))
                out.push_back(
                    {ActionTimelineEventKind::StateExit, track.id, state.id, state.type, state.end, state.payload});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
        if (left.time != right.time) return left.time < right.time;
        if (left.trackId != right.trackId) return left.trackId.format() < right.trackId.format();
        if (left.itemId != right.itemId) return left.itemId.format() < right.itemId.format();
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    });
    return Result<std::vector<ActionTimelineEvent>>::success(std::move(out));
}

Result<Value> ActionTimeline::toValue() const {
    auto valid = validate();
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Array encodedTracks;
    encodedTracks.reserve(tracks.size());
    for (const auto& track : tracks) {
        Value::Array notifies;
        Value::Array states;
        for (const auto& notify : track.notifies) notifies.push_back(encodeNotify(notify));
        for (const auto& state : track.states) states.push_back(encodeState(state));
        Value::Object object;
        object["id"]       = track.id.format();
        object["label"]    = track.label;
        object["kind"]     = std::string(actionTrackKindName(track.kind));
        object["muted"]    = track.muted;
        object["locked"]   = track.locked;
        object["notifies"] = Value(std::move(notifies));
        object["states"]   = Value(std::move(states));
        encodedTracks.emplace_back(std::move(object));
    }
    Value::Object root;
    root["schema"]        = std::string(kActionTimelineSchemaId);
    root["schemaVersion"] = static_cast<std::int64_t>(schemaVersion.value());
    root["actionId"]      = actionId.format();
    root["durationNs"]    = duration.nanoseconds();
    root["animationUri"]  = animationUri;
    root["tracks"]        = Value(std::move(encodedTracks));
    root["metadata"]      = Value(metadata);
    return Result<Value>::success(Value(std::move(root)));
}

Result<ActionTimeline> ActionTimeline::fromValue(const Value& value) {
    if (!value.isObject()) return invalid<ActionTimeline>("expected action timeline object", "timeline");
    auto schema = stringField(value, "schema", "schema");
    if (!schema) return Result<ActionTimeline>::failure(schema.status());
    if (schema.value() != kActionTimelineSchemaId)
        return invalid<ActionTimeline>("unexpected action timeline schema", "schema");
    auto version = intField(value, "schemaVersion", "schemaVersion");
    if (!version) return Result<ActionTimeline>::failure(version.status());
    auto actionId = logicalIdField(value, "actionId", "actionId");
    if (!actionId) return Result<ActionTimeline>::failure(actionId.status());
    auto duration = intField(value, "durationNs", "durationNs");
    if (!duration) return Result<ActionTimeline>::failure(duration.status());
    auto animationUri = stringField(value, "animationUri", "animationUri");
    if (!animationUri) return Result<ActionTimeline>::failure(animationUri.status());
    const Value* tracksValue   = field(value, "tracks");
    const Value* metadataValue = field(value, "metadata");
    if (!tracksValue || !tracksValue->isArray()) return invalid<ActionTimeline>("expected tracks array", "tracks");
    if (!metadataValue || !metadataValue->isObject())
        return invalid<ActionTimeline>("expected metadata object", "metadata");

    ActionTimeline candidate;
    candidate.schemaVersion = SchemaVersion(static_cast<std::uint64_t>(version.value()));
    candidate.actionId      = std::move(actionId).takeValue();
    candidate.duration      = Duration::fromNanoseconds(duration.value());
    candidate.animationUri  = std::move(animationUri).takeValue();
    candidate.metadata      = *metadataValue->getIf<Value::Object>();
    const auto& tracks      = *tracksValue->getIf<Value::Array>();
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const Value&      trackValue = tracks[trackIndex];
        const std::string path       = "tracks[" + std::to_string(trackIndex) + "]";
        if (!trackValue.isObject()) return invalid<ActionTimeline>("expected track object", path);
        auto id       = logicalIdField(trackValue, "id", path + ".id");
        auto label    = stringField(trackValue, "label", path + ".label");
        auto kindText = stringField(trackValue, "kind", path + ".kind");
        auto muted    = boolField(trackValue, "muted", path + ".muted");
        auto locked   = boolField(trackValue, "locked", path + ".locked");
        if (!id) return Result<ActionTimeline>::failure(id.status());
        if (!label) return Result<ActionTimeline>::failure(label.status());
        if (!kindText) return Result<ActionTimeline>::failure(kindText.status());
        if (!muted) return Result<ActionTimeline>::failure(muted.status());
        if (!locked) return Result<ActionTimeline>::failure(locked.status());
        auto kind = parseActionTrackKind(kindText.value());
        if (!kind) return Result<ActionTimeline>::failure(kind.status());
        const Value* notifies = field(trackValue, "notifies");
        const Value* states   = field(trackValue, "states");
        if (!notifies || !notifies->isArray())
            return invalid<ActionTimeline>("expected notifies array", path + ".notifies");
        if (!states || !states->isArray()) return invalid<ActionTimeline>("expected states array", path + ".states");
        ActionTrack track;
        track.id                 = std::move(id).takeValue();
        track.label              = std::move(label).takeValue();
        track.kind               = std::move(kind).takeValue();
        track.muted              = muted.value();
        track.locked             = locked.value();
        const auto& notifyValues = *notifies->getIf<Value::Array>();
        for (std::size_t index = 0; index < notifyValues.size(); ++index) {
            auto decoded = decodeNotify(notifyValues[index], path + ".notifies[" + std::to_string(index) + "]");
            if (!decoded) return Result<ActionTimeline>::failure(decoded.status());
            track.notifies.push_back(std::move(decoded).takeValue());
        }
        const auto& stateValues = *states->getIf<Value::Array>();
        for (std::size_t index = 0; index < stateValues.size(); ++index) {
            auto decoded = decodeState(stateValues[index], path + ".states[" + std::to_string(index) + "]");
            if (!decoded) return Result<ActionTimeline>::failure(decoded.status());
            track.states.push_back(std::move(decoded).takeValue());
        }
        candidate.tracks.push_back(std::move(track));
    }
    auto valid = candidate.validate();
    if (!valid) return Result<ActionTimeline>::failure(valid.status());
    return Result<ActionTimeline>::success(std::move(candidate));
}

}  // namespace eve::action
