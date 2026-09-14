#include "common/GameplayControlJson.h"

#include "common/Capability.h"

#include <algorithm>
#include <limits>
#include <set>

namespace eve {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<const Value::Object*> asObject(const Value& value, std::string path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object)
        return failure<const Value::Object*>(DiagnosticCode::ParseError,
                                             "gameplay control value must be an object", std::move(path));
    return Result<const Value::Object*>::success(object);
}

Result<const Value*> member(const Value::Object& object, std::string_view name, Value::Type type,
                            std::string_view path) {
    const auto found = object.find(std::string(name));
    if (found == object.end() || found->second.type() != type)
        return failure<const Value*>(DiagnosticCode::ParseError,
                                     "missing or invalid gameplay control field",
                                     std::string(path) + "." + std::string(name));
    return Result<const Value*>::success(&found->second);
}

Result<std::string> stringMember(const Value::Object& object, std::string_view name,
                                 std::string_view path) {
    auto found = member(object, name, Value::Type::String, path);
    if (!found) return Result<std::string>::failure(found.status());
    return Result<std::string>::success(found.value()->asString());
}

Result<std::uint64_t> uintMember(const Value::Object& object, std::string_view name,
                                 std::string_view path) {
    auto found = member(object, name, Value::Type::Int64, path);
    if (!found) return Result<std::uint64_t>::failure(found.status());
    if (found.value()->asInt() < 0)
        return failure<std::uint64_t>(DiagnosticCode::ParseError,
                                      "gameplay control integer must be non-negative",
                                      std::string(path) + "." + std::string(name));
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(found.value()->asInt()));
}

Result<void> knownFields(const Value::Object& object, const std::set<std::string>& allowed,
                         std::string_view path) {
    for (const auto& [name, value] : object) {
        (void)value;
        if (!allowed.contains(name))
            return failure<void>(DiagnosticCode::ParseError,
                                 "unknown gameplay control field",
                                 std::string(path) + "." + name);
    }
    return Result<void>::success();
}

Result<SubjectRef> subject(std::string_view text, std::string path) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed)
        return failure<SubjectRef>(DiagnosticCode::ParseError,
                                   "gameplay subject must be a canonical UUID", std::move(path));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*parsed));
}

Result<GameplaySession> session(const Value::Object& root) {
    const auto found = root.find("session");
    if (found == root.end())
        return failure<GameplaySession>(DiagnosticCode::ParseError,
                                        "gameplay request requires a session", "request.session");
    auto object = asObject(found->second, "request.session");
    if (!object) return Result<GameplaySession>::failure(object.status());
    auto known = knownFields(*object.value(), {"access", "controlledSubjects", "id"},
                             "request.session");
    if (!known) return Result<GameplaySession>::failure(known.status());
    auto id = stringMember(*object.value(), "id", "request.session");
    auto access = stringMember(*object.value(), "access", "request.session");
    auto subjects = member(*object.value(), "controlledSubjects", Value::Type::Array, "request.session");
    if (!id) return Result<GameplaySession>::failure(id.status());
    if (!access) return Result<GameplaySession>::failure(access.status());
    if (!subjects) return Result<GameplaySession>::failure(subjects.status());
    GameplaySession result;
    result.id = id.value();
    if (result.id.empty())
        return failure<GameplaySession>(DiagnosticCode::ParseError,
                                        "gameplay session id must not be empty", "request.session.id");
    if (access.value() == "player") result.access = GameplayAccess::PlayerEquivalent;
    else if (access.value() == "test-driver") result.access = GameplayAccess::TestDriver;
    else if (access.value() == "developer-cheat") result.access = GameplayAccess::DeveloperCheat;
    else
        return failure<GameplaySession>(DiagnosticCode::ParseError,
                                        "unknown gameplay access profile", "request.session.access");
    const auto* values = subjects.value()->getIf<Value::Array>();
    for (std::size_t index = 0; index < values->size(); ++index) {
        if (!(*values)[index].isString())
            return failure<GameplaySession>(DiagnosticCode::ParseError,
                                            "controlled subject must be a UUID string",
                                            "request.session.controlledSubjects." + std::to_string(index));
        auto parsed = subject((*values)[index].asString(),
                              "request.session.controlledSubjects." + std::to_string(index));
        if (!parsed) return Result<GameplaySession>::failure(parsed.status());
        result.controlledSubjects.push_back(parsed.value());
    }
    return Result<GameplaySession>::success(std::move(result));
}

Result<IGameplayControlProvider*> provider(std::string_view domain) {
    IGameplayControlProvider* match = nullptr;
    bool duplicate = false;
    cap::forEach<IGameplayControlProvider>([&](auto* candidate) {
        if (candidate && candidate->gameplayDomain() == domain) {
            duplicate = match != nullptr;
            match = candidate;
        }
    });
    if (duplicate)
        return failure<IGameplayControlProvider*>(DiagnosticCode::Conflict,
                                                  "multiple gameplay providers publish the same domain",
                                                  "request.domain");
    if (!match)
        return failure<IGameplayControlProvider*>(DiagnosticCode::NotFound,
                                                  "gameplay provider domain was not found", "request.domain");
    return Result<IGameplayControlProvider*>::success(match);
}

Value encodeObservation(GameplayObservation observation) {
    return Value(Value::Object{{"domain", Value(observation.domain.format())},
                               {"instance", Value(observation.instance.format())},
                               {"revision", Value(static_cast<std::int64_t>(observation.revision))},
                               {"state", std::move(observation.state)},
                               {"tick", Value(static_cast<std::int64_t>(observation.tick.value()))}});
}

Value encodeActions(std::vector<GameplayActionDescriptor> actions) {
    Value::Array output;
    for (auto& action : actions)
        output.emplace_back(Value::Object{{"id", Value(action.id.format())},
                                          {"parameterSchema", std::move(action.parameterSchema)}});
    return Value(std::move(output));
}

Value encodeReceipt(GameplayCommandReceipt receipt) {
    return Value(Value::Object{{"acceptedTick", Value(static_cast<std::int64_t>(receipt.acceptedTick.value()))},
                               {"commandId", Value(std::move(receipt.commandId))},
                               {"details", std::move(receipt.details)},
                               {"executionId", Value(std::move(receipt.executionId))},
                               {"resultingRevision", Value(static_cast<std::int64_t>(receipt.resultingRevision))}});
}

Value encodeEvents(std::vector<GameplayEvent> events) {
    Value::Array output;
    for (auto& event : events)
        output.emplace_back(Value::Object{
            {"causationCommandId", Value(std::move(event.causationCommandId))},
            {"correlationId", Value(std::move(event.correlationId))},
            {"payload", std::move(event.payload)},
            {"sequence", Value(static_cast<std::int64_t>(event.sequence))},
            {"subject", Value(event.subject.format())},
            {"tick", Value(static_cast<std::int64_t>(event.tick.value()))},
            {"type", Value(std::move(event.type))},
        });
    return Value(std::move(output));
}

Result<void> validateRoot(const Value::Object& root) {
    static const std::set<std::string> allowed{"afterSequence", "command", "deltaNanoseconds", "domain",
                                                "instance", "op", "schemaId", "schemaVersion", "session",
                                                "subject", "tick"};
    auto known = knownFields(root, allowed, "request");
    if (!known) return known;
    auto schemaId = stringMember(root, "schemaId", "request");
    auto version = member(root, "schemaVersion", Value::Type::Int64, "request");
    if (!schemaId) return Result<void>::failure(schemaId.status());
    if (!version) return Result<void>::failure(version.status());
    if (schemaId.value() != "evengine.gameplay-control-request" || version.value()->asInt() != 1)
        return failure<void>(DiagnosticCode::Unsupported,
                             "unsupported gameplay control request schema", "request.schemaVersion");
    return Result<void>::success();
}

Result<SubjectRef> rootSubject(const Value::Object& root, std::string_view name) {
    auto text = stringMember(root, name, "request");
    if (!text) return Result<SubjectRef>::failure(text.status());
    return subject(text.value(), "request." + std::string(name));
}

}  // namespace

Result<Value> executeGameplayControlRequest(const Value& request) {
    auto root = asObject(request, "request");
    if (!root) return Result<Value>::failure(root.status());
    auto valid = validateRoot(*root.value());
    if (!valid) return Result<Value>::failure(valid.status());
    auto operation = stringMember(*root.value(), "op", "request");
    if (!operation) return Result<Value>::failure(operation.status());
    if (operation.value() == "domains") {
        std::vector<std::string> domains;
        cap::forEach<IGameplayControlProvider>([&](auto* candidate) {
            if (candidate) domains.emplace_back(candidate->gameplayDomain());
        });
        std::sort(domains.begin(), domains.end());
        Value::Array output;
        for (auto& domain : domains) output.emplace_back(std::move(domain));
        return Result<Value>::success(Value(Value::Object{{"domains", Value(std::move(output))}}));
    }
    auto domain = stringMember(*root.value(), "domain", "request");
    if (!domain) return Result<Value>::failure(domain.status());
    auto target = provider(domain.value());
    if (!target) return Result<Value>::failure(target.status());
    auto access = session(*root.value());
    if (!access) return Result<Value>::failure(access.status());
    auto instance = rootSubject(*root.value(), "instance");
    if (!instance) return Result<Value>::failure(instance.status());

    if (operation.value() == "observe") {
        auto result = target.value()->observeGameplay(access.value(), instance.value());
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(encodeObservation(std::move(result).takeValue()));
    }
    if (operation.value() == "actions") {
        auto actor = rootSubject(*root.value(), "subject");
        if (!actor) return Result<Value>::failure(actor.status());
        auto result = target.value()->availableGameplayActions(access.value(), instance.value(), actor.value());
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(Value(Value::Object{{"actions", encodeActions(std::move(result).takeValue())}}));
    }
    if (operation.value() == "submit") {
        const auto found = root.value()->find("command");
        if (found == root.value()->end())
            return failure<Value>(DiagnosticCode::ParseError, "submit requires a command", "request.command");
        auto commandObject = asObject(found->second, "request.command");
        if (!commandObject) return Result<Value>::failure(commandObject.status());
        auto known = knownFields(*commandObject.value(),
                                 {"action", "expectedRevision", "id", "observedTick", "parameters", "subject"},
                                 "request.command");
        if (!known) return Result<Value>::failure(known.status());
        auto commandId = stringMember(*commandObject.value(), "id", "request.command");
        auto actionText = stringMember(*commandObject.value(), "action", "request.command");
        auto actorText = stringMember(*commandObject.value(), "subject", "request.command");
        auto observedTick = uintMember(*commandObject.value(), "observedTick", "request.command");
        auto expectedRevision = uintMember(*commandObject.value(), "expectedRevision", "request.command");
        const auto parameters = commandObject.value()->find("parameters");
        if (!commandId) return Result<Value>::failure(commandId.status());
        if (!actionText) return Result<Value>::failure(actionText.status());
        if (!actorText) return Result<Value>::failure(actorText.status());
        if (!observedTick) return Result<Value>::failure(observedTick.status());
        if (!expectedRevision) return Result<Value>::failure(expectedRevision.status());
        if (parameters == commandObject.value()->end())
            return failure<Value>(DiagnosticCode::ParseError, "submit command requires parameters",
                                  "request.command.parameters");
        const auto parsedAction = LogicalId::parse(actionText.value());
        if (!parsedAction)
            return failure<Value>(DiagnosticCode::ParseError, "command action must be a logical id",
                                  "request.command.action");
        auto actor = subject(actorText.value(), "request.command.subject");
        if (!actor) return Result<Value>::failure(actor.status());
        GameplayCommand command;
        command.id = commandId.value();
        command.action = *parsedAction;
        command.subject = actor.value();
        command.observedTick = SimulationTick(observedTick.value());
        command.expectedRevision = expectedRevision.value();
        command.parameters = parameters->second;
        auto result = target.value()->submitGameplay(access.value(), instance.value(), command);
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(Value(Value::Object{{"receipt", encodeReceipt(std::move(result).takeValue())}}));
    }
    if (operation.value() == "advance") {
        auto tick = uintMember(*root.value(), "tick", "request");
        auto delta = member(*root.value(), "deltaNanoseconds", Value::Type::Int64, "request");
        if (!tick) return Result<Value>::failure(tick.status());
        if (!delta) return Result<Value>::failure(delta.status());
        if (delta.value()->asInt() < 0)
            return failure<Value>(DiagnosticCode::ParseError, "delta must be non-negative",
                                  "request.deltaNanoseconds");
        SimulationStep step{SimulationTick(tick.value()), Duration::fromNanoseconds(delta.value()->asInt())};
        auto result = target.value()->advanceGameplay(access.value(), instance.value(), step);
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(encodeObservation(std::move(result).takeValue()));
    }
    if (operation.value() == "events") {
        auto cursor = uintMember(*root.value(), "afterSequence", "request");
        if (!cursor) return Result<Value>::failure(cursor.status());
        auto result = target.value()->gameplayEvents(access.value(), instance.value(), cursor.value());
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(Value(Value::Object{{"events", encodeEvents(std::move(result).takeValue())}}));
    }
    return failure<Value>(DiagnosticCode::Unsupported, "unsupported gameplay control operation", "request.op");
}

Result<std::string> executeGameplayControlJson(std::string_view requestJson) {
    auto request = Value::fromJson(requestJson);
    if (!request) return Result<std::string>::failure(request.status());
    auto response = executeGameplayControlRequest(request.value());
    if (!response) return Result<std::string>::failure(response.status());
    return response.value().toJson();
}

}  // namespace eve
