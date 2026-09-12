#include "action/ActionTimeline.h"

#include <algorithm>
#include <cmath>
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

Value encodeAnimationSection(const ActionAnimationSection& section) {
    Value::Object object;
    object["id"]            = section.id.format();
    object["animationUri"]  = section.animationUri;
    object["startNs"]       = section.start.nanoseconds();
    object["endNs"]         = section.end.nanoseconds();
    object["blendInNs"]     = section.blendIn.nanoseconds();
    object["sourceStartNs"] = section.sourceStart.nanoseconds();
    object["sourceEndNs"]   = section.sourceEnd.nanoseconds();
    object["blendCurve"]    = std::string(actionBlendCurveName(section.blendCurve));
    return Value(std::move(object));
}

Result<ActionAnimationSection> decodeAnimationSection(const Value& value, const std::string& path,
                                                      std::int64_t schemaVersion) {
    if (!value.isObject()) return invalid<ActionAnimationSection>("expected animation section object", path);
    auto id = logicalIdField(value, "id", path + ".id");
    if (!id) return Result<ActionAnimationSection>::failure(id.status());
    auto uri = stringField(value, "animationUri", path + ".animationUri");
    if (!uri) return Result<ActionAnimationSection>::failure(uri.status());
    auto start = intField(value, "startNs", path + ".startNs");
    if (!start) return Result<ActionAnimationSection>::failure(start.status());
    auto end = intField(value, "endNs", path + ".endNs");
    if (!end) return Result<ActionAnimationSection>::failure(end.status());
    auto blend = intField(value, "blendInNs", path + ".blendInNs");
    if (!blend) return Result<ActionAnimationSection>::failure(blend.status());
    ActionAnimationSection section{std::move(id).takeValue(), std::move(uri).takeValue(),
                                   Duration::fromNanoseconds(start.value()), Duration::fromNanoseconds(end.value()),
                                   Duration::fromNanoseconds(blend.value())};
    if (schemaVersion >= 3) {
        auto sourceStart = intField(value, "sourceStartNs", path + ".sourceStartNs");
        if (!sourceStart) return Result<ActionAnimationSection>::failure(sourceStart.status());
        auto sourceEnd = intField(value, "sourceEndNs", path + ".sourceEndNs");
        if (!sourceEnd) return Result<ActionAnimationSection>::failure(sourceEnd.status());
        auto blendCurve = stringField(value, "blendCurve", path + ".blendCurve");
        if (!blendCurve) return Result<ActionAnimationSection>::failure(blendCurve.status());
        auto parsedCurve = actionBlendCurveFromName(blendCurve.value());
        if (!parsedCurve) return invalid<ActionAnimationSection>("unknown animation blend curve", path + ".blendCurve");
        section.sourceStart = Duration::fromNanoseconds(sourceStart.value());
        section.sourceEnd   = Duration::fromNanoseconds(sourceEnd.value());
        section.blendCurve  = *parsedCurve;
    }
    return Result<ActionAnimationSection>::success(std::move(section));
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

std::string_view actionBlendCurveName(ActionBlendCurve curve) noexcept {
    switch (curve) {
        case ActionBlendCurve::Linear: return "linear";
        case ActionBlendCurve::EaseInOut: return "ease-in-out";
    }
    return "ease-in-out";
}

std::optional<ActionBlendCurve> actionBlendCurveFromName(std::string_view name) noexcept {
    if (name == "linear") return ActionBlendCurve::Linear;
    if (name == "ease-in-out") return ActionBlendCurve::EaseInOut;
    return std::nullopt;
}

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
    if (!std::isfinite(montage.basePlayRate) || montage.basePlayRate <= 0.0)
        return invalid("montage base play rate must be positive and finite", "montage.basePlayRate");
    if (montage.defaultBlendIn < Duration::zero() || montage.defaultBlendOut < Duration::zero() ||
        montage.blendOutOffset < Duration::zero())
        return invalid("montage blend durations must be non-negative", "montage");
    for (std::size_t index = 0; index < splitTimestamps.size(); ++index) {
        if (splitTimestamps[index] <= Duration::zero() || splitTimestamps[index] >= duration ||
            (index > 0 && splitTimestamps[index - 1] >= splitTimestamps[index]))
            return invalid("physical section splits must be strictly ordered inside the timeline",
                           "splitTimestampsNs[" + std::to_string(index) + "]");
    }

    std::set<std::string> ids;
    for (std::size_t index = 0; index < animationSections.size(); ++index) {
        const auto&       section = animationSections[index];
        const std::string path    = "animationSections[" + std::to_string(index) + "]";
        if (!section.id.isValid()) return invalid("animation section id is invalid", path + ".id");
        if (!ids.insert(section.id.format()).second) return invalid("timeline item ids must be unique", path + ".id");
        if (section.animationUri.empty()) return invalid("animation section requires a URI", path + ".animationUri");
        if (section.start < Duration::zero() || section.end <= section.start || section.end > duration)
            return invalid("animation section range is invalid", path);
        if (section.blendIn < Duration::zero() ||
            section.blendIn.nanoseconds() > section.end.nanoseconds() - section.start.nanoseconds())
            return invalid("animation section blend-in is outside its range", path + ".blendInNs");
        if (section.sourceStart < Duration::zero() || section.sourceEnd < Duration::zero() ||
            (!section.sourceEnd.isZero() && section.sourceEnd <= section.sourceStart))
            return invalid("animation section source trim is invalid", path + ".sourceStartNs");
        if (index > 0) {
            const auto& previous = animationSections[index - 1];
            if (previous.start > section.start)
                return invalid("animation sections must be sorted by start time", path + ".startNs");
            if (previous.end > section.start) return invalid("animation sections must not overlap", path + ".startNs");
        }
    }
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
    Value::Array encodedSections;
    Value::Array encodedSplits;
    encodedSections.reserve(animationSections.size());
    for (const auto& section : animationSections) encodedSections.push_back(encodeAnimationSection(section));
    encodedSplits.reserve(splitTimestamps.size());
    for (const Duration split : splitTimestamps) encodedSplits.emplace_back(split.nanoseconds());
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
    root["schema"]            = std::string(kActionTimelineSchemaId);
    root["schemaVersion"]     = static_cast<std::int64_t>(schemaVersion.value());
    root["actionId"]          = actionId.format();
    root["durationNs"]        = duration.nanoseconds();
    root["animationUri"]      = animationSections.empty() ? animationUri : animationSections.front().animationUri;
    root["animationSections"] = Value(std::move(encodedSections));
    root["splitTimestampsNs"] = Value(std::move(encodedSplits));
    root["montage"]           = Value::Object{{"basePlayRate", montage.basePlayRate},
                                              {"looping", montage.looping},
                                              {"footIk", montage.footIk},
                                              {"animationLayer", static_cast<std::int64_t>(montage.animationLayer)},
                                              {"defaultBlendInNs", montage.defaultBlendIn.nanoseconds()},
                                              {"defaultBlendOutNs", montage.defaultBlendOut.nanoseconds()},
                                              {"blendOutOffsetNs", montage.blendOutOffset.nanoseconds()},
                                              {"rootMotionHorizontal", montage.rootMotionHorizontal},
                                              {"rootMotionVertical", montage.rootMotionVertical},
                                              {"rootMotionRotation", montage.rootMotionRotation}};
    root["tracks"]            = Value(std::move(encodedTracks));
    root["metadata"]          = Value(metadata);
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
    if (version.value() < 1 || version.value() > static_cast<std::int64_t>(kActionTimelineSchemaVersion))
        return invalid<ActionTimeline>("unsupported action timeline schema version", "schemaVersion");
    candidate.schemaVersion = SchemaVersion(kActionTimelineSchemaVersion);
    candidate.actionId      = std::move(actionId).takeValue();
    candidate.duration      = Duration::fromNanoseconds(duration.value());
    candidate.animationUri  = std::move(animationUri).takeValue();
    candidate.metadata      = *metadataValue->getIf<Value::Object>();
    if (version.value() == 1) {
        if (!candidate.animationUri.empty() && !candidate.duration.isZero()) {
            candidate.animationSections.push_back({*LogicalId::fromParts("animation-section", "legacy"),
                                                   candidate.animationUri, Duration::zero(), candidate.duration,
                                                   Duration::zero()});
        }
    } else {
        const Value* sectionsValue = field(value, "animationSections");
        if (!sectionsValue || !sectionsValue->isArray())
            return invalid<ActionTimeline>("expected animationSections array", "animationSections");
        const auto& sections = *sectionsValue->getIf<Value::Array>();
        for (std::size_t index = 0; index < sections.size(); ++index) {
            auto decoded = decodeAnimationSection(sections[index], "animationSections[" + std::to_string(index) + "]",
                                                  version.value());
            if (!decoded) return Result<ActionTimeline>::failure(decoded.status());
            candidate.animationSections.push_back(std::move(decoded).takeValue());
        }
        if (!candidate.animationSections.empty())
            candidate.animationUri = candidate.animationSections.front().animationUri;
        if (version.value() >= 3) {
            const Value* splitsValue = field(value, "splitTimestampsNs");
            if (!splitsValue || !splitsValue->isArray())
                return invalid<ActionTimeline>("expected splitTimestampsNs array", "splitTimestampsNs");
            const auto& splits = *splitsValue->getIf<Value::Array>();
            for (std::size_t index = 0; index < splits.size(); ++index) {
                if (!splits[index].isInt64())
                    return invalid<ActionTimeline>("expected physical section timestamp",
                                                   "splitTimestampsNs[" + std::to_string(index) + "]");
                candidate.splitTimestamps.push_back(Duration::fromNanoseconds(splits[index].asInt()));
            }
            const Value* montageValue = field(value, "montage");
            if (!montageValue || !montageValue->isObject())
                return invalid<ActionTimeline>("expected montage settings object", "montage");
            const Value* basePlayRate   = field(*montageValue, "basePlayRate");
            auto         looping        = boolField(*montageValue, "looping", "montage.looping");
            auto         footIk         = boolField(*montageValue, "footIk", "montage.footIk");
            auto         layer          = intField(*montageValue, "animationLayer", "montage.animationLayer");
            auto         blendIn        = intField(*montageValue, "defaultBlendInNs", "montage.defaultBlendInNs");
            auto         blendOut       = intField(*montageValue, "defaultBlendOutNs", "montage.defaultBlendOutNs");
            auto         blendOutOffset = intField(*montageValue, "blendOutOffsetNs", "montage.blendOutOffsetNs");
            auto rootHorizontal = boolField(*montageValue, "rootMotionHorizontal", "montage.rootMotionHorizontal");
            auto rootVertical   = boolField(*montageValue, "rootMotionVertical", "montage.rootMotionVertical");
            auto rootRotation   = boolField(*montageValue, "rootMotionRotation", "montage.rootMotionRotation");
            if (!basePlayRate || (!basePlayRate->isDouble() && !basePlayRate->isInt64()) || !looping || !footIk ||
                !layer || !blendIn || !blendOut || !blendOutOffset || !rootHorizontal || !rootVertical ||
                !rootRotation || layer.value() < 0)
                return invalid<ActionTimeline>("montage settings are invalid", "montage");
            candidate.montage.basePlayRate =
                basePlayRate->isDouble() ? basePlayRate->asDouble() : static_cast<double>(basePlayRate->asInt());
            candidate.montage.looping              = looping.value();
            candidate.montage.footIk               = footIk.value();
            candidate.montage.animationLayer       = static_cast<std::uint32_t>(layer.value());
            candidate.montage.defaultBlendIn       = Duration::fromNanoseconds(blendIn.value());
            candidate.montage.defaultBlendOut      = Duration::fromNanoseconds(blendOut.value());
            candidate.montage.blendOutOffset       = Duration::fromNanoseconds(blendOutOffset.value());
            candidate.montage.rootMotionHorizontal = rootHorizontal.value();
            candidate.montage.rootMotionVertical   = rootVertical.value();
            candidate.montage.rootMotionRotation   = rootRotation.value();
        }
    }
    const auto& tracks = *tracksValue->getIf<Value::Array>();
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

Result<std::pair<Duration, Duration>> ActionTimeline::sectionRange(std::size_t index) const {
    if (index >= sectionCount())
        return invalid<std::pair<Duration, Duration>>("physical section index is outside the timeline", "index");
    const Duration start = index == 0 ? Duration::zero() : splitTimestamps[index - 1];
    const Duration end   = index < splitTimestamps.size() ? splitTimestamps[index] : duration;
    return Result<std::pair<Duration, Duration>>::success({start, end});
}

}  // namespace eve::action
