#include "rpg/StoryEvent.h"

#include "common/Json.h"
#include "rpg/GameState.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::rpg {
namespace {

std::unordered_map<std::string, StoryEventDefinition>& definitions() {
    static std::unordered_map<std::string, StoryEventDefinition> value;
    return value;
}

template <typename T>
eve::Result<T> storyFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "rpg.story-event"));
}

bool validText(const std::string& value, bool allowEmpty = false) {
    if (value.empty()) return allowEmpty;
    if (value.size() > 1024) return false;
    for (unsigned char ch : value)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool parseKind(const std::string& value, StoryEventStepKind& kind) {
    if (value == "dialogue")
        kind = StoryEventStepKind::Dialogue;
    else if (value == "message")
        kind = StoryEventStepKind::Message;
    else if (value == "wait")
        kind = StoryEventStepKind::Wait;
    else if (value == "move")
        kind = StoryEventStepKind::Move;
    else if (value == "camera")
        kind = StoryEventStepKind::Camera;
    else
        return false;
    return true;
}

std::string kindName(StoryEventStepKind kind) {
    switch (kind) {
        case StoryEventStepKind::Dialogue: return "dialogue";
        case StoryEventStepKind::Message: return "message";
        case StoryEventStepKind::Wait: return "wait";
        case StoryEventStepKind::Move: return "move";
        case StoryEventStepKind::Camera: return "camera";
    }
    return {};
}

std::string eventScope(const std::string& eventId) { return "story.event:" + eventId; }

}  // namespace

eve::Result<int> StoryEventCatalogue::replaceFromJsonStrict(const std::string& json) {
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    if (!document.valid())
        return storyFailure<int>(eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid JSON" : parseError,
                                 "$");
    const auto root = document.root();
    if (!root.isObject())
        return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story-event document root must be an object",
                                 "$");
    const std::unordered_set<std::string> rootFields = {"schema", "version", "events"};
    for (const auto& key : root.keys())
        if (!rootFields.contains(key))
            return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                     "story-event document contains an unknown field", "$." + key);
    const auto schema  = root.get("schema");
    const auto version = root.get("version");
    const auto events  = root.get("events");
    if (!schema.isString() || schema.asString() != "eve.rpg.story-events")
        return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story-event schema id is invalid", "$.schema");
    if (!version.isInt64() || version.asInt64() != 1)
        return storyFailure<int>(eve::DiagnosticCode::UnknownVersion, "unsupported story-event schema version",
                                 "$.version");
    if (!events.isArray() || events.size() == 0 || events.size() > 256)
        return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "events must contain between 1 and 256 entries",
                                 "$.events");

    const std::unordered_set<std::string> eventFields = {"id", "repeatable", "steps"};
    const std::unordered_set<std::string> stepFields  = {"kind", "reference", "actorId", "x", "y", "duration"};
    std::unordered_map<std::string, StoryEventDefinition> proposed;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto        object = events.at(index);
        const std::string path   = "$.events[" + std::to_string(index) + "]";
        if (!object.isObject())
            return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story event must be an object", path);
        for (const auto& key : object.keys())
            if (!eventFields.contains(key))
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story event contains an unknown field",
                                         path + "." + key);
        const auto id         = object.get("id");
        const auto repeatable = object.get("repeatable");
        const auto steps      = object.get("steps");
        if (!id.isString() || !validText(id.asString()))
            return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                     "story event id must be a stable non-empty id", path + ".id");
        if (proposed.contains(id.asString()))
            return storyFailure<int>(eve::DiagnosticCode::AlreadyExists, "duplicate story event id", path + ".id");
        if (!repeatable.isBool())
            return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "repeatable must be boolean",
                                     path + ".repeatable");
        if (!steps.isArray() || steps.size() == 0 || steps.size() > 128)
            return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                     "steps must contain between 1 and 128 entries", path + ".steps");
        StoryEventDefinition definition;
        definition.id         = id.asString();
        definition.repeatable = repeatable.asBool();
        for (std::size_t stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
            const auto        value    = steps.at(stepIndex);
            const std::string stepPath = path + ".steps[" + std::to_string(stepIndex) + "]";
            if (!value.isObject())
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story-event step must be an object",
                                         stepPath);
            for (const auto& key : value.keys())
                if (!stepFields.contains(key))
                    return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                             "story-event step contains an unknown field", stepPath + "." + key);
            const auto     kindValue = value.get("kind");
            const auto     reference = value.get("reference");
            const auto     actorId   = value.get("actorId");
            const auto     x         = value.get("x");
            const auto     y         = value.get("y");
            const auto     duration  = value.get("duration");
            StoryEventStep parsed;
            if (!kindValue.isString() || !parseKind(kindValue.asString(), parsed.kind))
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story-event step kind is invalid",
                                         stepPath + ".kind");
            if (!reference.isString() || !actorId.isString() || !validText(reference.asString(), true) ||
                !validText(actorId.asString(), true) || !x.isNumber() || !y.isNumber() || !duration.isNumber() ||
                !std::isfinite(x.asDouble()) || !std::isfinite(y.asDouble()) || !std::isfinite(duration.asDouble()) ||
                duration.asDouble() < 0.0)
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "story-event step fields are invalid",
                                         stepPath);
            const bool needsReference =
                parsed.kind == StoryEventStepKind::Dialogue || parsed.kind == StoryEventStepKind::Message;
            const bool needsActor    = parsed.kind == StoryEventStepKind::Move;
            const bool needsDuration = parsed.kind == StoryEventStepKind::Wait;
            if (needsReference != !reference.asString().empty())
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                         "reference presence does not match step kind", stepPath + ".reference");
            if (needsActor != !actorId.asString().empty())
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                         "actorId presence does not match step kind", stepPath + ".actorId");
            if (needsDuration && duration.asDouble() <= 0.0)
                return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "wait duration must be positive",
                                         stepPath + ".duration");
            parsed.reference = reference.asString();
            parsed.actorId   = actorId.asString();
            parsed.x         = x.asDouble();
            parsed.y         = y.asDouble();
            parsed.duration  = duration.asDouble();
            definition.steps.push_back(std::move(parsed));
        }
        proposed.emplace(definition.id, std::move(definition));
    }
    definitions() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(definitions().size()));
}

void StoryEventCatalogue::clear() { definitions().clear(); }
int  StoryEventCatalogue::count() { return static_cast<int>(definitions().size()); }
bool StoryEventCatalogue::contains(const std::string& eventId) { return definitions().contains(eventId); }
const StoryEventDefinition* StoryEventCatalogue::find(const std::string& eventId) {
    const auto found = definitions().find(eventId);
    return found == definitions().end() ? nullptr : &found->second;
}

eve::Result<void> StoryEventSession::begin(const std::string& eventId, GameState* gameState) {
    if (!gameState)
        return storyFailure<void>(eve::DiagnosticCode::InvalidArgument, "begin requires a GameState owner",
                                  "gameState");
    const auto* definition = StoryEventCatalogue::find(eventId);
    if (!definition)
        return storyFailure<void>(eve::DiagnosticCode::NotFound, "story event is not registered", "eventId");
    const std::string scope          = eventScope(eventId);
    const bool        hasCompleted   = gameState->hasSelfVariable(scope, "completed");
    const double      completedValue = hasCompleted ? gameState->getSelfVariable(scope, "completed") : 0.0;
    if (hasCompleted && completedValue != 0.0 && completedValue != 1.0)
        return storyFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                  "persisted story-event completion flag is invalid", scope + ".completed");
    const bool completed = completedValue == 1.0;
    if (completed && !definition->repeatable)
        return storyFailure<void>(eve::DiagnosticCode::Conflict, "non-repeatable story event is already complete",
                                  "eventId");
    double storedCursor =
        gameState->hasSelfVariable(scope, "cursor") ? gameState->getSelfVariable(scope, "cursor") : 0.0;
    if (completed && definition->repeatable) storedCursor = 0.0;
    if (!std::isfinite(storedCursor) || std::floor(storedCursor) != storedCursor || storedCursor < 0.0 ||
        storedCursor >= static_cast<double>(definition->steps.size()))
        return storyFailure<void>(eve::DiagnosticCode::InvariantViolation, "persisted story-event cursor is invalid",
                                  scope + ".cursor");

    StoryEventDefinition candidate = *definition;
    definition_                    = std::move(candidate);
    cursor_                        = static_cast<int>(storedCursor);
    active_                        = true;
    finished_                      = false;
    if (completed) {
        gameState->setSelfVariable(scope, "cursor", 0.0);
        gameState->setSelfVariable(scope, "completed", 0.0);
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> StoryEventSession::advance(GameState* gameState) {
    if (!gameState)
        return storyFailure<int>(eve::DiagnosticCode::InvalidArgument, "advance requires a GameState owner",
                                 "gameState");
    if (!active_ || !current())
        return storyFailure<int>(eve::DiagnosticCode::PreconditionViolation, "story event has no active step",
                                 "session");
    const std::string scope = eventScope(definition_.id);
    const double      persistedCursor =
        gameState->hasSelfVariable(scope, "cursor") ? gameState->getSelfVariable(scope, "cursor") : 0.0;
    const double persistedCompleted =
        gameState->hasSelfVariable(scope, "completed") ? gameState->getSelfVariable(scope, "completed") : 0.0;
    if (!std::isfinite(persistedCursor) || persistedCursor != static_cast<double>(cursor_) || persistedCompleted != 0.0)
        return storyFailure<int>(eve::DiagnosticCode::Conflict,
                                 "story-event cursor changed since this step was presented", scope + ".cursor");
    const int next = cursor_ + 1;
    gameState->setSelfVariable(scope, "cursor", static_cast<double>(next));
    if (next == static_cast<int>(definition_.steps.size())) {
        gameState->setSelfVariable(scope, "completed", 1.0);
        cursor_   = next;
        active_   = false;
        finished_ = true;
        return eve::Result<int>::success(0, eve::Status::success(eve::StatusCode::Applied));
    }
    cursor_ = next;
    return eve::Result<int>::success(static_cast<int>(definition_.steps.size()) - cursor_,
                                     eve::Status::success(eve::StatusCode::Applied));
}

const StoryEventStep* StoryEventSession::current() const noexcept {
    if (!active_ || cursor_ < 0 || static_cast<std::size_t>(cursor_) >= definition_.steps.size()) return nullptr;
    return &definition_.steps[static_cast<std::size_t>(cursor_)];
}

bool        StoryEventSession::isActive() const { return active_; }
bool        StoryEventSession::isFinished() const { return finished_; }
int         StoryEventSession::getStepIndex() const { return cursor_; }
int         StoryEventSession::getStepCount() const { return static_cast<int>(definition_.steps.size()); }
std::string StoryEventSession::getEventId() const { return definition_.id; }
std::string StoryEventSession::getStepKind() const {
    const auto* step = current();
    return step ? kindName(step->kind) : std::string{};
}
std::string StoryEventSession::getReference() const {
    const auto* step = current();
    return step ? step->reference : std::string{};
}
std::string StoryEventSession::getActorId() const {
    const auto* step = current();
    return step ? step->actorId : std::string{};
}
double StoryEventSession::getX() const {
    const auto* step = current();
    return step ? step->x : 0.0;
}
double StoryEventSession::getY() const {
    const auto* step = current();
    return step ? step->y : 0.0;
}
double StoryEventSession::getDuration() const {
    const auto* step = current();
    return step ? step->duration : 0.0;
}

}  // namespace eve::rpg
