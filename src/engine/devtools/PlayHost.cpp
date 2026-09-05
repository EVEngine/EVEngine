#include "devtools/PlayHost.h"

#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/RenderCapture.h"
#include "common/ScriptError.h"
#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/Debugger.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/PlayTrace.h"
#include "devtools/Snapshot.hpp"

#include <squirrel.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace eve::dev {
namespace {

constexpr std::string_view kPlaySchemaId     = "evengine.play-request";
constexpr std::string_view kPlayResponseId   = "evengine.play-response";
constexpr std::string_view kContractSchemaId = "evengine.game-agent-contract";
constexpr std::int64_t     kSchemaVersion    = 1;
constexpr std::int64_t     kMaxFrameSteps    = 1024;

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<const Value::Object*> asObject(const Value& value, std::string path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object)
        return failure<const Value::Object*>(DiagnosticCode::ParseError, "play value must be an object",
                                             std::move(path));
    return Result<const Value::Object*>::success(object);
}

Result<void> knownFields(const Value::Object& object, const std::set<std::string>& allowed,
                         std::string_view path) {
    for (const auto& [name, value] : object) {
        (void)value;
        if (!allowed.contains(name))
            return failure<void>(DiagnosticCode::ParseError, "unknown play field",
                                 std::string(path) + "." + name);
    }
    return Result<void>::success();
}

Result<const Value*> member(const Value::Object& object, std::string_view name, Value::Type type,
                            std::string_view path) {
    const auto found = object.find(std::string(name));
    if (found == object.end() || found->second.type() != type)
        return failure<const Value*>(DiagnosticCode::ParseError, "missing or invalid play field",
                                     std::string(path) + "." + std::string(name));
    return Result<const Value*>::success(&found->second);
}

Result<std::string> stringMember(const Value::Object& object, std::string_view name,
                                 std::string_view path) {
    auto found = member(object, name, Value::Type::String, path);
    if (!found) return Result<std::string>::failure(found.status());
    return Result<std::string>::success(found.value()->asString());
}

Result<std::int64_t> intMember(const Value::Object& object, std::string_view name, std::string_view path) {
    auto found = member(object, name, Value::Type::Int64, path);
    if (!found) return Result<std::int64_t>::failure(found.status());
    return Result<std::int64_t>::success(found.value()->asInt());
}

Result<void> optionalString(const Value::Object& object, std::string_view name, std::string_view path,
                            std::string* out) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) return Result<void>::success();
    if (!found->second.isString())
        return failure<void>(DiagnosticCode::ParseError, "play field must be a string",
                             std::string(path) + "." + std::string(name));
    *out = found->second.asString();
    return Result<void>::success();
}

std::vector<std::string> splitPath(std::string_view dotted) {
    std::vector<std::string> parts;
    std::string              current;
    for (char ch : dotted) {
        if (ch == '.') {
            if (!current.empty()) parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) parts.push_back(std::move(current));
    return parts;
}

Result<Value> walkPath(const Value& root, std::string_view dotted, std::string path) {
    const Value* current = &root;
    auto         parts   = splitPath(dotted);
    if (parts.empty())
        return failure<Value>(DiagnosticCode::ParseError, "observation field path must not be empty", std::move(path));
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const auto* object = current->getIf<Value::Object>();
        if (!object)
            return failure<Value>(DiagnosticCode::NotFound, "observation path is not an object",
                                  path + "." + std::string(dotted));
        const auto found = object->find(parts[index]);
        if (found == object->end())
            return failure<Value>(DiagnosticCode::NotFound, "observation field was not found",
                                  path + "." + std::string(dotted));
        current = &found->second;
    }
    return Result<Value>::success(*current);
}

Result<Value> projectFields(const Value& root, const std::vector<std::string>& fields) {
    if (fields.empty()) return Result<Value>::success(root);
    Value::Object projected;
    for (const auto& field : fields) {
        auto walked = walkPath(root, field, "observation.fields");
        if (!walked) return Result<Value>::failure(walked.status());
        projected.emplace(field, std::move(walked).takeValue());
    }
    return Result<Value>::success(Value(std::move(projected)));
}

Value playResponse(std::string op, Value::Object extra) {
    extra.emplace("op", Value(std::move(op)));
    extra.emplace("schemaId", Value(std::string(kPlayResponseId)));
    extra.emplace("schemaVersion", Value(kSchemaVersion));
    return Value(std::move(extra));
}

struct ObservationSpec {
    std::string              id;
    std::string              kind;
    std::string              path;
    std::vector<std::string> fields;
};

struct ActionSpec {
    std::string id;
    std::string script;
    std::string action;
};

struct ParsedContract {
    std::string                  id;
    std::string                  defaultStep;
    std::string                  actionSource;
    std::string                  actionDomain;
    std::vector<ObservationSpec> observations;
    std::vector<ActionSpec>      actions;
    std::vector<std::string>     captureRequiredFor;
};

Result<std::vector<std::string>> stringArray(const Value& value, std::string path) {
    const auto* array = value.getIf<Value::Array>();
    if (!array)
        return failure<std::vector<std::string>>(DiagnosticCode::ParseError, "play array must contain strings",
                                                 std::move(path));
    std::vector<std::string> out;
    for (std::size_t index = 0; index < array->size(); ++index) {
        if (!(*array)[index].isString())
            return failure<std::vector<std::string>>(
                DiagnosticCode::ParseError, "play array entry must be a string",
                path + "." + std::to_string(index));
        out.push_back((*array)[index].asString());
    }
    return Result<std::vector<std::string>>::success(std::move(out));
}

Result<ParsedContract> parseContract(const Value& contract) {
    auto root = asObject(contract, "contract");
    if (!root) return Result<ParsedContract>::failure(root.status());
    auto known =
        knownFields(*root.value(),
                    {"actions", "capture", "clock", "entry", "id", "observations", "schemaId", "schemaVersion",
                     "smoke", "tolerances"},
                    "contract");
    if (!known) return Result<ParsedContract>::failure(known.status());
    auto schemaId = stringMember(*root.value(), "schemaId", "contract");
    auto version  = intMember(*root.value(), "schemaVersion", "contract");
    auto id       = stringMember(*root.value(), "id", "contract");
    auto entry    = stringMember(*root.value(), "entry", "contract");
    if (!schemaId) return Result<ParsedContract>::failure(schemaId.status());
    if (!version) return Result<ParsedContract>::failure(version.status());
    if (!id) return Result<ParsedContract>::failure(id.status());
    if (!entry) return Result<ParsedContract>::failure(entry.status());
    if (schemaId.value() != kContractSchemaId || version.value() != kSchemaVersion)
        return failure<ParsedContract>(DiagnosticCode::Unsupported, "unsupported game agent contract schema",
                                       "contract.schemaVersion");
    if (id.value().empty())
        return failure<ParsedContract>(DiagnosticCode::ParseError, "contract id must not be empty", "contract.id");

    ParsedContract parsed;
    parsed.id          = id.value();
    parsed.defaultStep = "frame";

    const auto clock = root.value()->find("clock");
    if (clock != root.value()->end()) {
        auto object = asObject(clock->second, "contract.clock");
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto clockKnown = knownFields(*object.value(), {"defaultStep"}, "contract.clock");
        if (!clockKnown) return Result<ParsedContract>::failure(clockKnown.status());
        auto step = stringMember(*object.value(), "defaultStep", "contract.clock");
        if (!step) return Result<ParsedContract>::failure(step.status());
        parsed.defaultStep = step.value();
        if (parsed.defaultStep != "frame")
            return failure<ParsedContract>(DiagnosticCode::Unsupported,
                                           "P0 play host only supports frame defaultStep",
                                           "contract.clock.defaultStep");
    }

    const auto observations = root.value()->find("observations");
    if (observations == root.value()->end() || !observations->second.isArray())
        return failure<ParsedContract>(DiagnosticCode::ParseError, "contract requires observations",
                                       "contract.observations");
    const auto* array = observations->second.getIf<Value::Array>();
    for (std::size_t index = 0; index < array->size(); ++index) {
        const std::string itemPath = "contract.observations." + std::to_string(index);
        auto              object   = asObject((*array)[index], itemPath);
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto itemKnown = knownFields(*object.value(), {"fields", "id", "kind", "path"}, itemPath);
        if (!itemKnown) return Result<ParsedContract>::failure(itemKnown.status());
        ObservationSpec spec;
        auto            specId = stringMember(*object.value(), "id", itemPath);
        auto            kind   = stringMember(*object.value(), "kind", itemPath);
        auto            path   = stringMember(*object.value(), "path", itemPath);
        if (!specId) return Result<ParsedContract>::failure(specId.status());
        if (!kind) return Result<ParsedContract>::failure(kind.status());
        if (!path) return Result<ParsedContract>::failure(path.status());
        spec.id   = specId.value();
        spec.kind = kind.value();
        spec.path = path.value();
        const auto fields = object.value()->find("fields");
        if (fields != object.value()->end()) {
            auto parsedFields = stringArray(fields->second, itemPath + ".fields");
            if (!parsedFields) return Result<ParsedContract>::failure(parsedFields.status());
            spec.fields = std::move(parsedFields).takeValue();
        }
        parsed.observations.push_back(std::move(spec));
    }

    const auto smoke = root.value()->find("smoke");
    if (smoke != root.value()->end()) {
        auto object = asObject(smoke->second, "contract.smoke");
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto smokeKnown =
            knownFields(*object.value(), {"maxHostFrames", "mustReach", "scene", "seed"}, "contract.smoke");
        if (!smokeKnown) return Result<ParsedContract>::failure(smokeKnown.status());
        auto scene = stringMember(*object.value(), "scene", "contract.smoke");
        if (!scene) return Result<ParsedContract>::failure(scene.status());
        const auto mustReach = object.value()->find("mustReach");
        if (mustReach != object.value()->end()) {
            auto reached = stringArray(mustReach->second, "contract.smoke.mustReach");
            if (!reached) return Result<ParsedContract>::failure(reached.status());
        }
    }

    const auto capture = root.value()->find("capture");
    if (capture != root.value()->end()) {
        auto object = asObject(capture->second, "contract.capture");
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto captureKnown = knownFields(*object.value(), {"backend", "requiredFor"}, "contract.capture");
        if (!captureKnown) return Result<ParsedContract>::failure(captureKnown.status());
        auto backend = stringMember(*object.value(), "backend", "contract.capture");
        if (!backend) return Result<ParsedContract>::failure(backend.status());
        if (backend.value() != "engine-readback")
            return failure<ParsedContract>(DiagnosticCode::Unsupported, "capture backend must be engine-readback",
                                           "contract.capture.backend");
        const auto requiredFor = object.value()->find("requiredFor");
        if (requiredFor != object.value()->end()) {
            auto names = stringArray(requiredFor->second, "contract.capture.requiredFor");
            if (!names) return Result<ParsedContract>::failure(names.status());
            parsed.captureRequiredFor = std::move(names).takeValue();
        }
    }

    const auto actions = root.value()->find("actions");
    if (actions != root.value()->end()) {
        auto object = asObject(actions->second, "contract.actions");
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto actionsKnown = knownFields(*object.value(), {"domain", "map", "source"}, "contract.actions");
        if (!actionsKnown) return Result<ParsedContract>::failure(actionsKnown.status());
        auto source = stringMember(*object.value(), "source", "contract.actions");
        if (!source) return Result<ParsedContract>::failure(source.status());
        parsed.actionSource = source.value();
        if (parsed.actionSource != "script-map" && parsed.actionSource != "gameplay-domain")
            return failure<ParsedContract>(DiagnosticCode::Unsupported, "unsupported actions.source",
                                           "contract.actions.source");
        auto domainField = optionalString(*object.value(), "domain", "contract.actions", &parsed.actionDomain);
        if (!domainField) return Result<ParsedContract>::failure(domainField.status());
        const auto map = object.value()->find("map");
        if (map != object.value()->end()) {
            const auto* array = map->second.getIf<Value::Array>();
            if (!array)
                return failure<ParsedContract>(DiagnosticCode::ParseError, "actions.map must be an array",
                                               "contract.actions.map");
            for (std::size_t index = 0; index < array->size(); ++index) {
                const std::string itemPath = "contract.actions.map." + std::to_string(index);
                auto item = asObject((*array)[index], itemPath);
                if (!item) return Result<ParsedContract>::failure(item.status());
                auto itemKnown = knownFields(*item.value(), {"action", "id", "script"}, itemPath);
                if (!itemKnown) return Result<ParsedContract>::failure(itemKnown.status());
                ActionSpec spec;
                auto actionId = stringMember(*item.value(), "id", itemPath);
                if (!actionId) return Result<ParsedContract>::failure(actionId.status());
                spec.id = actionId.value();
                auto scriptField = optionalString(*item.value(), "script", itemPath, &spec.script);
                if (!scriptField) return Result<ParsedContract>::failure(scriptField.status());
                auto mappedAction = optionalString(*item.value(), "action", itemPath, &spec.action);
                if (!mappedAction) return Result<ParsedContract>::failure(mappedAction.status());
                if (parsed.actionSource == "script-map" && spec.script.empty())
                    return failure<ParsedContract>(DiagnosticCode::ParseError, "script-map action requires script",
                                                   itemPath + ".script");
                if (parsed.actionSource == "gameplay-domain" && spec.action.empty())
                    return failure<ParsedContract>(DiagnosticCode::ParseError,
                                                   "gameplay-domain action requires action", itemPath + ".action");
                parsed.actions.push_back(std::move(spec));
            }
        }
    }

    const auto tolerances = root.value()->find("tolerances");
    if (tolerances != root.value()->end()) {
        auto object = asObject(tolerances->second, "contract.tolerances");
        if (!object) return Result<ParsedContract>::failure(object.status());
        auto tolKnown = knownFields(*object.value(), {"numericAbs", "screenshot"}, "contract.tolerances");
        if (!tolKnown) return Result<ParsedContract>::failure(tolKnown.status());
    }

    return Result<ParsedContract>::success(std::move(parsed));
}

Result<const ObservationSpec*> findObservation(const ParsedContract& contract, std::string_view id) {
    for (const auto& spec : contract.observations) {
        if (spec.id == id) return Result<const ObservationSpec*>::success(&spec);
    }
    return failure<const ObservationSpec*>(DiagnosticCode::NotFound, "observation id was not declared",
                                           "request.observation");
}

Result<const ActionSpec*> findAction(const ParsedContract& contract, std::string_view id) {
    for (const auto& spec : contract.actions) {
        if (spec.id == id) return Result<const ActionSpec*>::success(&spec);
    }
    return failure<const ActionSpec*>(DiagnosticCode::NotFound, "action id was not declared", "request.action");
}

Result<Value> requireContract(IPlayHostRuntime& runtime) {
    auto loaded = runtime.loadContract();
    if (!loaded) {
        if (loaded.code() == StatusCode::NotFound)
            return failure<Value>(DiagnosticCode::Unsupported,
                                  "play observe requires game.agent.json", "contract");
        return Result<Value>::failure(loaded.status());
    }
    return loaded;
}

Value encodeDomains(const std::vector<std::string>& domains) {
    Value::Array output;
    for (const auto& domain : domains) output.emplace_back(domain);
    return Value(std::move(output));
}

Result<Value> opStatus(IPlayHostRuntime& runtime) {
    Value::Object extra{{"paused", Value(runtime.paused())},
                        {"hostFrame", Value(static_cast<std::int64_t>(runtime.hostFrame()))},
                        {"domains", encodeDomains(runtime.gameplayDomains())}};
    auto loaded = runtime.loadContract();
    if (loaded) {
        auto parsed = parseContract(loaded.value());
        if (!parsed) return Result<Value>::failure(parsed.status());
        extra.emplace("contractId", Value(parsed.value().id));
        extra.emplace("contractLoaded", Value(true));
    } else if (loaded.code() == StatusCode::NotFound) {
        extra.emplace("contractLoaded", Value(false));
    } else {
        return Result<Value>::failure(loaded.status());
    }
    return Result<Value>::success(playResponse("status", std::move(extra)));
}

Result<Value> opClock(const Value::Object& root, IPlayHostRuntime& runtime) {
    auto mode = stringMember(root, "mode", "request");
    if (!mode) return Result<Value>::failure(mode.status());
    if (mode.value() == "pause") runtime.pause();
    else if (mode.value() == "play") runtime.play();
    else
        return failure<Value>(DiagnosticCode::ParseError, "clock mode must be pause or play", "request.mode");
    return Result<Value>::success(
        playResponse("clock", {{"mode", Value(mode.value())}, {"paused", Value(runtime.paused())}}));
}

Result<Value> opStep(const Value::Object& root, IPlayHostRuntime& runtime) {
    std::string clock = "frame";
    auto        clockField = optionalString(root, "clock", "request", &clock);
    if (!clockField) return Result<Value>::failure(clockField.status());
    if (clock != "frame")
        return failure<Value>(DiagnosticCode::Unsupported, "P0 play host only steps the host frame clock",
                              "request.clock");
    auto count = intMember(root, "count", "request");
    if (!count) return Result<Value>::failure(count.status());
    if (count.value() < 1 || count.value() > kMaxFrameSteps)
        return failure<Value>(DiagnosticCode::ParseError, "step count must be between 1 and 1024", "request.count");
    auto stepped = runtime.stepFrames(count.value());
    if (!stepped) return Result<Value>::failure(stepped.status());
    return Result<Value>::success(playResponse(
        "step", {{"clock", Value(std::move(clock))},
                 {"count", Value(count.value())},
                 {"hostFrame", Value(static_cast<std::int64_t>(runtime.hostFrame()))},
                 {"paused", Value(runtime.paused())}}));
}

Result<Value> opObserve(const Value::Object& root, IPlayHostRuntime& runtime) {
    auto observation = stringMember(root, "observation", "request");
    if (!observation) return Result<Value>::failure(observation.status());
    auto contractValue = requireContract(runtime);
    if (!contractValue) return Result<Value>::failure(contractValue.status());
    auto parsed = parseContract(contractValue.value());
    if (!parsed) return Result<Value>::failure(parsed.status());
    auto spec = findObservation(parsed.value(), observation.value());
    if (!spec) return Result<Value>::failure(spec.status());
    if (spec.value()->kind != "script-root")
        return failure<Value>(DiagnosticCode::Unsupported, "P0 play host only observes script-root",
                              "request.observation");
    auto state = runtime.observeScriptRoot(spec.value()->path, spec.value()->fields);
    if (!state) return Result<Value>::failure(state.status());
    return Result<Value>::success(playResponse("observe", {{"observation", Value(observation.value())},
                                                           {"state", std::move(state).takeValue()}}));
}

Result<Value> opCapture(const Value::Object& root, IPlayHostRuntime& runtime) {
    std::string path = "mcp_screenshot.png";
    auto        pathField = optionalString(root, "path", "request", &path);
    if (!pathField) return Result<Value>::failure(pathField.status());
    if (path.empty()) path = "mcp_screenshot.png";
    auto captured = runtime.capturePng(std::move(path));
    if (!captured) return Result<Value>::failure(captured.status());
    auto object = asObject(captured.value(), "capture");
    if (!object) return Result<Value>::failure(object.status());
    Value::Object extra = *object.value();
    extra.emplace("backend", Value("engine-readback"));
    return Result<Value>::success(playResponse("capture", std::move(extra)));
}

Result<Value> opCheckpoint(const Value::Object& root, IPlayHostRuntime& runtime) {
    auto mode = stringMember(root, "mode", "request");
    if (!mode) return Result<Value>::failure(mode.status());
    if (mode.value() == "capture") {
        auto json = runtime.captureCheckpoint();
        if (!json) return Result<Value>::failure(json.status());
        return Result<Value>::success(
            playResponse("checkpoint", {{"mode", Value("capture")}, {"json", Value(std::move(json).takeValue())}}));
    }
    if (mode.value() == "restore") {
        auto json = stringMember(root, "json", "request");
        if (!json) return Result<Value>::failure(json.status());
        auto restored = runtime.restoreCheckpoint(json.value());
        if (!restored) return Result<Value>::failure(restored.status());
        return Result<Value>::success(playResponse("checkpoint", {{"mode", Value("restore")}}));
    }
    return failure<Value>(DiagnosticCode::ParseError, "checkpoint mode must be capture or restore",
                          "request.mode");
}

Result<Value> opAct(const Value::Object& root, IPlayHostRuntime& runtime) {
    auto action = stringMember(root, "action", "request");
    if (!action) return Result<Value>::failure(action.status());
    auto contractValue = requireContract(runtime);
    if (!contractValue) return Result<Value>::failure(contractValue.status());
    auto parsed = parseContract(contractValue.value());
    if (!parsed) return Result<Value>::failure(parsed.status());
    auto spec = findAction(parsed.value(), action.value());
    if (!spec) return Result<Value>::failure(spec.status());
    if (parsed.value().actionSource == "script-map") {
        auto invoked = runtime.invokeScriptAction(spec.value()->id, spec.value()->script);
        if (!invoked) return Result<Value>::failure(invoked.status());
        return Result<Value>::success(playResponse("act", {{"action", Value(action.value())},
                                                           {"source", Value("script-map")},
                                                           {"receipt", std::move(invoked).takeValue()}}));
    }
    if (parsed.value().actionSource != "gameplay-domain")
        return failure<Value>(DiagnosticCode::Unsupported, "unsupported actions.source", "contract.actions.source");
    const auto commandField = root.find("command");
    if (commandField == root.end())
        return failure<Value>(DiagnosticCode::ParseError, "gameplay-domain act requires command", "request.command");
    auto commandObject = asObject(commandField->second, "request.command");
    if (!commandObject) return Result<Value>::failure(commandObject.status());
    Value::Object command = *commandObject.value();
    if (command.find("action") == command.end()) command.emplace("action", Value(spec.value()->action));
    std::string domain = parsed.value().actionDomain;
    auto domainField = optionalString(root, "domain", "request", &domain);
    if (!domainField) return Result<Value>::failure(domainField.status());
    if (domain.empty())
        return failure<Value>(DiagnosticCode::ParseError, "gameplay-domain act requires domain", "request.domain");
    const auto sessionField = root.find("session");
    const auto instanceField = root.find("instance");
    if (sessionField == root.end() || instanceField == root.end())
        return failure<Value>(DiagnosticCode::ParseError, "gameplay-domain act requires session and instance",
                              "request.session");
    Value::Object gameplay{{"schemaId", Value("evengine.gameplay-control-request")},
                           {"schemaVersion", Value(std::int64_t{1})},
                           {"op", Value("submit")},
                           {"domain", Value(std::move(domain))},
                           {"instance", instanceField->second},
                           {"session", sessionField->second},
                           {"command", Value(std::move(command))}};
    auto submitted = executeGameplayControlRequest(Value(std::move(gameplay)));
    if (!submitted) return Result<Value>::failure(submitted.status());
    return Result<Value>::success(playResponse("act", {{"action", Value(action.value())},
                                                       {"source", Value("gameplay-domain")},
                                                       {"gameplay", std::move(submitted).takeValue()}}));
}

Result<Value> opTrace() {
    return Result<Value>::success(
        playResponse("trace", {{"recording", PlayTraceBuffer::instance().exportTrace()}}));
}

Result<Value> opReplay(const Value::Object& root, IPlayHostRuntime& runtime) {
    const auto recording = root.find("recording");
    if (recording == root.end())
        return failure<Value>(DiagnosticCode::ParseError, "replay requires recording", "request.recording");
    return replayPlayTrace(recording->second, runtime);
}

bool sessionWantsTrace() {
    const auto& session = AgentDevelopmentSession::instance();
    if (!session.active()) return false;
    const auto phase = session.phase();
    return phase == AgentDevelopmentPhase::Run || phase == AgentDevelopmentPhase::Observe ||
           phase == AgentDevelopmentPhase::Verify;
}

void tryRecordEvidence(std::string criterionId, std::string kind, std::string summary, std::string artifact) {
    auto& session = AgentDevelopmentSession::instance();
    if (!session.active() || PlayTraceBuffer::instance().replaying() || criterionId.empty()) return;
    AgentDevelopmentEvidence evidence;
    evidence.criterionId = std::move(criterionId);
    evidence.kind        = std::move(kind);
    evidence.status      = "pass";
    evidence.summary     = std::move(summary);
    evidence.artifact    = std::move(artifact);
    (void)session.record(session.sessionId(), std::move(evidence));
}

bool hasCriterion(const AgentDevelopmentSession& session, std::string_view id) {
    for (const auto& criterion : session.criteria()) {
        if (criterion.id == id) return true;
    }
    return false;
}

void maybeAutoEvidence(const Value::Object& request, const Value& response, IPlayHostRuntime& runtime) {
    auto& session = AgentDevelopmentSession::instance();
    if (!session.active() || PlayTraceBuffer::instance().replaying()) return;
    const auto opField = request.find("op");
    if (opField == request.end() || !opField->second.isString()) return;
    const std::string& op = opField->second.asString();
    std::string criterionId;
    optionalString(request, "criterionId", "request", &criterionId).ignore("optional play criterionId");
    const auto* responseObject = response.getIf<Value::Object>();
    if (op == "observe") {
        std::string observation;
        optionalString(request, "observation", "request", &observation).ignore("optional play observation");
        if (criterionId.empty() && hasCriterion(session, observation)) criterionId = observation;
        tryRecordEvidence(std::move(criterionId), "runtime-observation", "play observe " + observation, {});
        return;
    }
    if (op == "capture") {
        std::string path;
        if (responseObject) {
            const auto found = responseObject->find("path");
            if (found != responseObject->end() && found->second.isString()) path = found->second.asString();
        }
        if (criterionId.empty() && hasCriterion(session, "visual")) criterionId = "visual";
        if (criterionId.empty()) {
            auto loaded = runtime.loadContract();
            if (loaded) {
                auto parsed = parseContract(loaded.value());
                if (parsed) {
                    for (const auto& name : parsed.value().captureRequiredFor) {
                        if (hasCriterion(session, name)) {
                            criterionId = name;
                            break;
                        }
                    }
                }
            }
        }
        tryRecordEvidence(std::move(criterionId), "screenshot", "play capture", std::move(path));
        return;
    }
    if (op == "checkpoint") {
        if (criterionId.empty() && hasCriterion(session, "checkpoint")) criterionId = "checkpoint";
        if (criterionId.empty() && hasCriterion(session, "recovery")) criterionId = "recovery";
        tryRecordEvidence(std::move(criterionId), "checkpoint", "play checkpoint", {});
        return;
    }
    if (op == "batch") {
        if (criterionId.empty() && hasCriterion(session, "smoke")) criterionId = "smoke";
        tryRecordEvidence(std::move(criterionId), "play-trace", "play batch", {});
    }
}

void afterPlaySuccess(const Value& request, const Value& response, IPlayHostRuntime& runtime) {
    auto root = request.getIf<Value::Object>();
    if (!root) return;
    if (PlayTraceBuffer::instance().replaying()) return;
    std::string trace;
    optionalString(*root, "trace", "request", &trace).ignore("trace already validated by dispatch");
    const auto opField = root->find("op");
    const std::string op = (opField != root->end() && opField->second.isString()) ? opField->second.asString() : "";
    const bool skipTrace = op == "status" || op == "clock" || op == "trace" || op == "replay" || op == "batch";
    bool append = trace == "append" || (trace.empty() && sessionWantsTrace());
    if (trace == "off") append = false;
    if (append && !skipTrace) {
        if (!PlayTraceBuffer::instance().recording()) {
            std::string contractId;
            std::string contractHash;
            auto loaded = runtime.loadContract();
            if (loaded) {
                auto parsed = parseContract(loaded.value());
                if (parsed) contractId = parsed.value().id;
                auto digest = playObservationDigest(loaded.value());
                if (digest) contractHash = std::move(digest).takeValue();
            }
            PlayTraceBuffer::instance().begin(std::move(contractId), std::move(contractHash), 0, {});
        }
        PlayTraceBuffer::instance().append(request, response, runtime.hostFrame());
    }
    maybeAutoEvidence(*root, response, runtime);
}

Result<Value> dispatchOne(const Value& request, IPlayHostRuntime& runtime, bool inBatch);

Result<Value> opBatch(const Value::Object& root, IPlayHostRuntime& runtime) {
    auto requests = member(root, "requests", Value::Type::Array, "request");
    if (!requests) return Result<Value>::failure(requests.status());
    const auto* array = requests.value()->getIf<Value::Array>();
    Value::Array results;
    for (std::size_t index = 0; index < array->size(); ++index) {
        auto one = dispatchOne((*array)[index], runtime, true);
        if (!one) return Result<Value>::failure(one.status());
        results.push_back(std::move(one).takeValue());
    }
    const auto count = static_cast<std::int64_t>(results.size());
    return Result<Value>::success(
        playResponse("batch", {{"count", Value(count)}, {"results", Value(std::move(results))}}));
}

Result<Value> dispatchOne(const Value& request, IPlayHostRuntime& runtime, bool inBatch) {
    auto root = asObject(request, "request");
    if (!root) return Result<Value>::failure(root.status());
    static const std::set<std::string> allowed{
        "action",         "clock",        "command",     "count",        "criterionId", "domain",
        "instance",       "json",         "mode",        "observation",  "op",          "path",
        "recording",      "requests",     "schemaId",    "schemaVersion","session",     "trace"};
    auto known = knownFields(*root.value(), allowed, "request");
    if (!known) return Result<Value>::failure(known.status());
    auto schemaId = stringMember(*root.value(), "schemaId", "request");
    auto version  = intMember(*root.value(), "schemaVersion", "request");
    if (!schemaId) return Result<Value>::failure(schemaId.status());
    if (!version) return Result<Value>::failure(version.status());
    if (schemaId.value() != kPlaySchemaId || version.value() != kSchemaVersion)
        return failure<Value>(DiagnosticCode::Unsupported, "unsupported play request schema",
                              "request.schemaVersion");
    std::string trace;
    auto        traceField = optionalString(*root.value(), "trace", "request", &trace);
    if (!traceField) return Result<Value>::failure(traceField.status());
    if (!trace.empty() && trace != "off" && trace != "append")
        return failure<Value>(DiagnosticCode::ParseError, "trace must be off or append", "request.trace");
    auto op = stringMember(*root.value(), "op", "request");
    if (!op) return Result<Value>::failure(op.status());
    auto executed = [&]() -> Result<Value> {
        if (op.value() == "batch") {
            if (inBatch)
                return failure<Value>(DiagnosticCode::Unsupported, "nested play batch is not allowed", "request.op");
            return opBatch(*root.value(), runtime);
        }
        if (op.value() == "status") return opStatus(runtime);
        if (op.value() == "clock") return opClock(*root.value(), runtime);
        if (op.value() == "step") return opStep(*root.value(), runtime);
        if (op.value() == "observe") return opObserve(*root.value(), runtime);
        if (op.value() == "capture") return opCapture(*root.value(), runtime);
        if (op.value() == "checkpoint") return opCheckpoint(*root.value(), runtime);
        if (op.value() == "act") return opAct(*root.value(), runtime);
        if (op.value() == "trace") return opTrace();
        if (op.value() == "replay") return opReplay(*root.value(), runtime);
        return failure<Value>(DiagnosticCode::Unsupported, "unsupported play operation", "request.op");
    }();
    if (!executed) return executed;
    afterPlaySuccess(request, executed.value(), runtime);
    return executed;
}

Result<Value> snapshotRoot(HSQUIRRELVM vm, std::string_view rootName) {
    if (!vm) return failure<Value>(DiagnosticCode::Unsupported, "play observe requires an attached script VM",
                                   "observation");
    std::string error;
    std::string json = Snapshot::instance().capture(vm, &error);
    if (json.empty())
        return failure<Value>(DiagnosticCode::Failed,
                              error.empty() ? "script snapshot capture failed" : std::move(error),
                              "observation");
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<Value>::failure(parsed.status());
    auto object = asObject(parsed.value(), "snapshot");
    if (!object) return Result<Value>::failure(object.status());
    const auto roots = object.value()->find("roots");
    if (roots == object.value()->end())
        return failure<Value>(DiagnosticCode::NotFound, "snapshot does not contain roots", "observation");
    auto rootsObject = asObject(roots->second, "snapshot.roots");
    if (!rootsObject) return Result<Value>::failure(rootsObject.status());
    const auto found = rootsObject.value()->find(std::string(rootName));
    if (found == rootsObject.value()->end())
        return failure<Value>(DiagnosticCode::NotFound, "marked script root was not in the snapshot",
                              "observation.path");
    return Result<Value>::success(found->second);
}

class EnginePlayHostRuntime final : public IPlayHostRuntime {
public:
    bool paused() const override { return Debugger::instance().isPaused(); }
    std::uint64_t hostFrame() const override { return hostFrame_; }
    void pause() override { Debugger::instance().pause(PauseReason::PauseKey); }
    void play() override { Debugger::instance().resume(); }

    Result<void> stepFrames(std::int64_t count) override {
        Debugger::instance().stepFrames(static_cast<int>(count));
        hostFrame_ += static_cast<std::uint64_t>(count);
        return Result<void>::success();
    }

    Result<Value> loadContract() const override {
        std::filesystem::path root = McpServer::instance().gameRoot();
        if (root.empty()) {
            std::error_code ec;
            root = std::filesystem::current_path(ec);
            if (ec)
                return failure<Value>(DiagnosticCode::Failed, "cannot resolve game root", "contract");
        }
        const auto path = root / "game.agent.json";
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
            return failure<Value>(DiagnosticCode::NotFound, "game.agent.json was not found", "contract");
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return failure<Value>(DiagnosticCode::Failed, "cannot read game.agent.json", "contract");
        std::ostringstream contents;
        contents << in.rdbuf();
        return Value::fromJson(contents.str());
    }

    Result<Value> observeScriptRoot(std::string_view root,
                                    const std::vector<std::string>& fields) const override {
        auto value = snapshotRoot(Debugger::instance().vm(), root);
        if (!value) return Result<Value>::failure(value.status());
        return projectFields(value.value(), fields);
    }

    Result<Value> capturePng(std::string path) override {
        auto* capture = eve::cap::query<IRenderCapture>();
        if (!capture)
            return failure<Value>(DiagnosticCode::Unsupported, "IRenderCapture is not available", "capture");
        int         width = 0, height = 0;
        std::string error;
        if (!capture->savePng(path, &width, &height, &error))
            return failure<Value>(DiagnosticCode::Failed,
                                  error.empty() ? "engine screenshot failed" : std::move(error), "capture");
        return Result<Value>::success(Value(Value::Object{{"height", Value(static_cast<std::int64_t>(height))},
                                                          {"path", Value(std::move(path))},
                                                          {"width", Value(static_cast<std::int64_t>(width))}}));
    }

    Result<std::string> captureCheckpoint() override {
        HSQUIRRELVM vm = Debugger::instance().vm();
        if (!vm)
            return failure<std::string>(DiagnosticCode::Unsupported, "play checkpoint requires an attached script VM",
                                        "checkpoint");
        std::string error;
        std::string json = Snapshot::instance().capture(vm, &error);
        if (json.empty())
            return failure<std::string>(DiagnosticCode::Failed,
                                        error.empty() ? "checkpoint capture failed" : std::move(error),
                                        "checkpoint");
        return Result<std::string>::success(std::move(json));
    }

    Result<void> restoreCheckpoint(std::string_view json) override {
        HSQUIRRELVM vm = Debugger::instance().vm();
        if (!vm)
            return failure<void>(DiagnosticCode::Unsupported, "play checkpoint requires an attached script VM",
                                 "checkpoint");
        std::string error;
        if (!Snapshot::instance().restore(vm, std::string(json), &error))
            return failure<void>(DiagnosticCode::Failed,
                                 error.empty() ? "checkpoint restore failed" : std::move(error), "checkpoint");
        return Result<void>::success();
    }

    Result<Value> invokeScriptAction(std::string_view id, std::string_view source) override {
        HSQUIRRELVM vm = Debugger::instance().vm();
        if (!vm)
            return failure<Value>(DiagnosticCode::Unsupported, "play act requires an attached script VM",
                                  "request.action");
        const SQInteger top = sq_gettop(vm);
        const std::string snippet(source);
        if (SQ_FAILED(sq_compilebuffer(vm, snippet.c_str(), static_cast<SQInteger>(snippet.size()),
                                       "<play-action>", SQTrue))) {
            sq_settop(vm, top);
            return failure<Value>(DiagnosticCode::Failed, eve::script::formatScriptError(eve::script::captureCompileError(vm)),
                                  "request.action");
        }
        sq_pushroottable(vm);
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return failure<Value>(DiagnosticCode::Failed,
                                  eve::script::formatScriptError(eve::script::takeLastScriptError(vm)),
                                  "request.action");
        }
        sq_settop(vm, top);
        return Result<Value>::success(Value(Value::Object{{"action", Value(std::string(id))}}));
    }

    std::vector<std::string> gameplayDomains() const override {
        std::vector<std::string> domains;
        eve::cap::forEach<IGameplayControlProvider>([&](auto* candidate) {
            if (candidate) domains.emplace_back(candidate->gameplayDomain());
        });
        std::sort(domains.begin(), domains.end());
        return domains;
    }

private:
    std::uint64_t hostFrame_ = 0;
};

EnginePlayHostRuntime& liveRuntime() {
    static EnginePlayHostRuntime runtime;
    return runtime;
}

}  // namespace

Result<Value> executePlayRequest(const Value& request, IPlayHostRuntime& runtime) {
    return dispatchOne(request, runtime, false);
}

Result<Value> executePlayRequest(const Value& request) {
    return executePlayRequest(request, liveRuntime());
}

Result<std::string> executePlayJson(std::string_view requestJson) {
    auto request = Value::fromJson(requestJson);
    if (!request) return Result<std::string>::failure(request.status());
    auto response = executePlayRequest(request.value());
    if (!response) return Result<std::string>::failure(response.status());
    return response.value().toJson();
}

}  // namespace eve::dev
