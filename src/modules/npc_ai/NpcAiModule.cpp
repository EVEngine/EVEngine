#include "npc_ai/NpcAiModule.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "npc_ai/NpcAi.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace eve::npc_ai {
namespace {

Result<BlackboardType> parseBlackboardType(const std::string& text) {
    if (text == "Boolean") return Result<BlackboardType>::success(BlackboardType::Boolean);
    if (text == "Integer") return Result<BlackboardType>::success(BlackboardType::Integer);
    if (text == "Number") return Result<BlackboardType>::success(BlackboardType::Number);
    if (text == "String") return Result<BlackboardType>::success(BlackboardType::String);
    return Result<BlackboardType>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                             "npc_ai blackboard type is invalid", "type", {},
                                                             "npc_ai.squirrel"));
}

Result<CompareOp> parseCompareOp(const std::string& text) {
    if (text == "Exists") return Result<CompareOp>::success(CompareOp::Exists);
    if (text == "Equal") return Result<CompareOp>::success(CompareOp::Equal);
    if (text == "NotEqual") return Result<CompareOp>::success(CompareOp::NotEqual);
    if (text == "Less") return Result<CompareOp>::success(CompareOp::Less);
    if (text == "LessEqual") return Result<CompareOp>::success(CompareOp::LessEqual);
    if (text == "Greater") return Result<CompareOp>::success(CompareOp::Greater);
    if (text == "GreaterEqual") return Result<CompareOp>::success(CompareOp::GreaterEqual);
    return Result<CompareOp>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "npc_ai compare op is invalid", "op", {}, "npc_ai.squirrel"));
}

Result<BlackboardValue> parseBlackboardValue(const Value& value) {
    if (const auto* flag = value.getIf<bool>()) return Result<BlackboardValue>::success(*flag);
    if (const auto* integer = value.getIf<std::int64_t>()) return Result<BlackboardValue>::success(*integer);
    if (const auto* number = value.getIf<double>()) return Result<BlackboardValue>::success(*number);
    if (const auto* text = value.getIf<std::string>()) return Result<BlackboardValue>::success(*text);
    return Result<BlackboardValue>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "npc_ai blackboard value type is unsupported", "value",
                                                              {}, "npc_ai.squirrel"));
}

Result<std::uint32_t> parseUInt32Field(std::int64_t value, const std::string& path) {
    constexpr auto kMax = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    if (value < 0 || value > kMax)
        return Result<std::uint32_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai unsigned 32-bit field is out of range", path, {},
            "npc_ai.squirrel"));
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

Result<BlackboardPredicate> parsePredicate(const Value& value, const std::string& path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object)
        return Result<BlackboardPredicate>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai predicate must be an object", path, {}, "npc_ai.squirrel"));
    BlackboardPredicate predicate;
    const auto          key = object->find("key");
    const auto          op  = object->find("op");
    if (key == object->end() || !key->second.getIf<std::string>())
        return Result<BlackboardPredicate>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai predicate key is required", path + ".key", {}, "npc_ai.squirrel"));
    if (op == object->end() || !op->second.getIf<std::string>())
        return Result<BlackboardPredicate>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai predicate op is required", path + ".op", {}, "npc_ai.squirrel"));
    predicate.key = *key->second.getIf<std::string>();
    auto parsedOp = parseCompareOp(*op->second.getIf<std::string>());
    if (!parsedOp) return Result<BlackboardPredicate>::failure(parsedOp.status());
    predicate.op = parsedOp.value();
    if (const auto found = object->find("value"); found != object->end()) {
        auto parsed = parseBlackboardValue(found->second);
        if (!parsed) return Result<BlackboardPredicate>::failure(parsed.status());
        predicate.value = std::move(parsed).takeValue();
    }
    return Result<BlackboardPredicate>::success(std::move(predicate));
}

Result<BehaviorDefinition> parseBehaviorDefinition(const Value& value) {
    const auto* root = value.getIf<Value::Object>();
    if (!root)
        return Result<BehaviorDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "npc_ai behavior must be an object", "behavior",
                                                                     {}, "npc_ai.squirrel"));
    BehaviorDefinition definition;
    const auto         id = root->find("id");
    if (id == root->end() || !id->second.getIf<std::string>())
        return Result<BehaviorDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai behavior id is required", "id", {}, "npc_ai.squirrel"));
    definition.id = *id->second.getIf<std::string>();
    if (const auto version = root->find("schemaVersion"); version != root->end()) {
        if (const auto* integer = version->second.getIf<std::int64_t>()) {
            auto parsedVersion = parseUInt32Field(*integer, "schemaVersion");
            if (!parsedVersion) return Result<BehaviorDefinition>::failure(parsedVersion.status());
            definition.schemaVersion = parsedVersion.value();
        } else
            return Result<BehaviorDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                         "npc_ai schemaVersion must be an integer",
                                                                         "schemaVersion", {}, "npc_ai.squirrel"));
    }
    const auto initial = root->find("initialState");
    if (initial == root->end() || !initial->second.getIf<std::string>())
        return Result<BehaviorDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "npc_ai initialState is required", "initialState",
                                                                     {}, "npc_ai.squirrel"));
    definition.initialState = *initial->second.getIf<std::string>();

    if (const auto schema = root->find("blackboardSchema"); schema != root->end()) {
        const auto* array = schema->second.getIf<Value::Array>();
        if (!array)
            return Result<BehaviorDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                         "npc_ai blackboardSchema must be an array",
                                                                         "blackboardSchema", {}, "npc_ai.squirrel"));
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<Value::Object>();
            if (!item)
                return Result<BehaviorDefinition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "npc_ai blackboard schema entry must be an object",
                    "blackboardSchema[" + std::to_string(index) + "]", {}, "npc_ai.squirrel"));
            BlackboardKeySpec keySpec;
            const auto        key  = item->find("key");
            const auto        type = item->find("type");
            if (key == item->end() || !key->second.getIf<std::string>() || type == item->end() ||
                !type->second.getIf<std::string>())
                return Result<BehaviorDefinition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "npc_ai blackboard schema key/type are required",
                    "blackboardSchema[" + std::to_string(index) + "]", {}, "npc_ai.squirrel"));
            keySpec.key = *key->second.getIf<std::string>();
            auto parsed = parseBlackboardType(*type->second.getIf<std::string>());
            if (!parsed) return Result<BehaviorDefinition>::failure(parsed.status());
            keySpec.type = parsed.value();
            if (const auto required = item->find("required"); required != item->end()) {
                if (const auto* flag = required->second.getIf<bool>())
                    keySpec.required = *flag;
                else
                    return Result<BehaviorDefinition>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "npc_ai blackboard required must be a bool",
                        "blackboardSchema[" + std::to_string(index) + "].required", {}, "npc_ai.squirrel"));
            }
            if (const auto defaultValue = item->find("default"); defaultValue != item->end()) {
                auto parsedDefault = parseBlackboardValue(defaultValue->second);
                if (!parsedDefault) return Result<BehaviorDefinition>::failure(parsedDefault.status());
                keySpec.defaultValue = std::move(parsedDefault).takeValue();
            }
            definition.blackboardSchema.push_back(std::move(keySpec));
        }
    }

    const auto states = root->find("states");
    if (states == root->end() || !states->second.getIf<Value::Array>())
        return Result<BehaviorDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai states must be an array", "states", {}, "npc_ai.squirrel"));
    const auto& stateArray = *states->second.getIf<Value::Array>();
    for (std::size_t index = 0; index < stateArray.size(); ++index) {
        const std::string path = "states[" + std::to_string(index) + "]";
        const auto*       item = stateArray[index].getIf<Value::Object>();
        if (!item)
            return Result<BehaviorDefinition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "npc_ai state must be an object", path, {}, "npc_ai.squirrel"));
        StateDefinition state;
        const auto      stateId = item->find("id");
        if (stateId == item->end() || !stateId->second.getIf<std::string>())
            return Result<BehaviorDefinition>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "npc_ai state id is required", path + ".id", {}, "npc_ai.squirrel"));
        state.id = *stateId->second.getIf<std::string>();
        if (const auto parent = item->find("parent"); parent != item->end()) {
            if (const auto* text = parent->second.getIf<std::string>())
                state.parent = *text;
            else
                return Result<BehaviorDefinition>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                             "npc_ai state parent must be a string",
                                                                             path + ".parent", {}, "npc_ai.squirrel"));
        }
        if (const auto enter = item->find("enterConditions"); enter != item->end()) {
            const auto* array = enter->second.getIf<Value::Array>();
            if (!array)
                return Result<BehaviorDefinition>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "npc_ai enterConditions must be an array",
                                      path + ".enterConditions", {}, "npc_ai.squirrel"));
            for (std::size_t predicateIndex = 0; predicateIndex < array->size(); ++predicateIndex) {
                auto predicate =
                    parsePredicate((*array)[predicateIndex], path + ".enterConditions[" + std::to_string(predicateIndex) + "]");
                if (!predicate) return Result<BehaviorDefinition>::failure(predicate.status());
                state.enterConditions.push_back(std::move(predicate).takeValue());
            }
        }
        if (const auto tasks = item->find("tasks"); tasks != item->end()) {
            const auto* array = tasks->second.getIf<Value::Array>();
            if (!array)
                return Result<BehaviorDefinition>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "npc_ai tasks must be an array", path + ".tasks", {},
                    "npc_ai.squirrel"));
            for (std::size_t taskIndex = 0; taskIndex < array->size(); ++taskIndex) {
                const auto* taskObject = (*array)[taskIndex].getIf<Value::Object>();
                if (!taskObject)
                    return Result<BehaviorDefinition>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "npc_ai task must be an object",
                        path + ".tasks[" + std::to_string(taskIndex) + "]", {}, "npc_ai.squirrel"));
                TaskSpec task;
                const auto taskId   = taskObject->find("id");
                const auto taskType = taskObject->find("type");
                if (taskId == taskObject->end() || !taskId->second.getIf<std::string>() ||
                    taskType == taskObject->end() || !taskType->second.getIf<std::string>())
                    return Result<BehaviorDefinition>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "npc_ai task id/type are required",
                        path + ".tasks[" + std::to_string(taskIndex) + "]", {}, "npc_ai.squirrel"));
                task.id   = *taskId->second.getIf<std::string>();
                task.type = *taskType->second.getIf<std::string>();
                if (const auto parameters = taskObject->find("parametersJson"); parameters != taskObject->end()) {
                    if (const auto* text = parameters->second.getIf<std::string>())
                        task.parametersJson = *text;
                    else
                        return Result<BehaviorDefinition>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "npc_ai task parametersJson must be a string",
                            path + ".tasks[" + std::to_string(taskIndex) + "].parametersJson", {}, "npc_ai.squirrel"));
                }
                state.tasks.push_back(std::move(task));
            }
        }
        if (const auto transitions = item->find("transitions"); transitions != item->end()) {
            const auto* array = transitions->second.getIf<Value::Array>();
            if (!array)
                return Result<BehaviorDefinition>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "npc_ai transitions must be an array",
                                      path + ".transitions", {}, "npc_ai.squirrel"));
            for (std::size_t transitionIndex = 0; transitionIndex < array->size(); ++transitionIndex) {
                const std::string transitionPath =
                    path + ".transitions[" + std::to_string(transitionIndex) + "]";
                const auto* transitionObject = (*array)[transitionIndex].getIf<Value::Object>();
                if (!transitionObject)
                    return Result<BehaviorDefinition>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "npc_ai transition must be an object", transitionPath, {},
                        "npc_ai.squirrel"));
                Transition transition;
                const auto target = transitionObject->find("targetState");
                if (target == transitionObject->end() || !target->second.getIf<std::string>())
                    return Result<BehaviorDefinition>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "npc_ai transition targetState is required",
                        transitionPath + ".targetState", {}, "npc_ai.squirrel"));
                transition.targetState = *target->second.getIf<std::string>();
                if (const auto signal = transitionObject->find("signal"); signal != transitionObject->end()) {
                    if (const auto* text = signal->second.getIf<std::string>())
                        transition.signal = *text;
                    else
                        return Result<BehaviorDefinition>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "npc_ai transition signal must be a string",
                            transitionPath + ".signal", {}, "npc_ai.squirrel"));
                }
                if (const auto priority = transitionObject->find("priority"); priority != transitionObject->end()) {
                    if (const auto* integer = priority->second.getIf<std::int64_t>()) {
                        auto parsedPriority = parseUInt32Field(*integer, transitionPath + ".priority");
                        if (!parsedPriority) return Result<BehaviorDefinition>::failure(parsedPriority.status());
                        transition.priority = parsedPriority.value();
                    } else
                        return Result<BehaviorDefinition>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "npc_ai transition priority must be an integer",
                            transitionPath + ".priority", {}, "npc_ai.squirrel"));
                }
                if (const auto conditions = transitionObject->find("conditions");
                    conditions != transitionObject->end()) {
                    const auto* conditionArray = conditions->second.getIf<Value::Array>();
                    if (!conditionArray)
                        return Result<BehaviorDefinition>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "npc_ai transition conditions must be an array",
                            transitionPath + ".conditions", {}, "npc_ai.squirrel"));
                    for (std::size_t conditionIndex = 0; conditionIndex < conditionArray->size(); ++conditionIndex) {
                        auto predicate = parsePredicate(
                            (*conditionArray)[conditionIndex],
                            transitionPath + ".conditions[" + std::to_string(conditionIndex) + "]");
                        if (!predicate) return Result<BehaviorDefinition>::failure(predicate.status());
                        transition.conditions.push_back(std::move(predicate).takeValue());
                    }
                }
                state.transitions.push_back(std::move(transition));
            }
        }
        definition.states.push_back(std::move(state));
    }
    return Result<BehaviorDefinition>::success(std::move(definition));
}

Value blackboardValueToValue(const BlackboardValue& value) {
    return std::visit(
        [](const auto& item) -> Value {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>)
                return Value(item);
            else if constexpr (std::is_same_v<T, std::int64_t>)
                return Value(item);
            else if constexpr (std::is_same_v<T, double>)
                return Value(item);
            else
                return Value(item);
        },
        value);
}

Result<AgentHandle> parseAgentHandle(std::int64_t packed) {
    if (packed <= 0)
        return Result<AgentHandle>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "npc_ai agent handle must be positive", "agent", {}, "npc_ai.squirrel"));
    return Result<AgentHandle>::success(AgentHandle::fromPacked(static_cast<std::uint64_t>(packed)));
}

class ScriptNpcAiWorld {
public:
    explicit ScriptNpcAiWorld(std::int64_t traceCapacity, std::int64_t maxMemoriesPerAgent) {
        NpcAiWorldConfig config;
        if (traceCapacity > 0) config.traceCapacity = static_cast<std::size_t>(traceCapacity);
        // Zero disables perception memory in the core world; only negative keeps the default.
        if (maxMemoriesPerAgent >= 0) config.maxMemoriesPerAgent = static_cast<std::size_t>(maxMemoriesPerAgent);
        world_ = std::make_unique<NpcAiWorld>(config);
    }

    Result<Value> registerBehavior(const std::string& definitionJson) {
        auto decoded = Value::fromJson(definitionJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        auto definition = parseBehaviorDefinition(decoded.value());
        if (!definition) return Result<Value>::failure(definition.status());
        const std::string id = definition.value().id;
        auto              registered = world_->registerBehavior(std::move(definition).takeValue());
        if (!registered) return Result<Value>::failure(registered.status());
        Value::Object result;
        result["behaviorId"] = id;
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> validateBehavior(const std::string& definitionJson) const {
        auto decoded = Value::fromJson(definitionJson);
        if (!decoded) return Result<Value>::failure(decoded.status());
        auto definition = parseBehaviorDefinition(decoded.value());
        if (!definition) return Result<Value>::failure(definition.status());
        auto validated = NpcAiWorld::validate(definition.value());
        if (!validated) return Result<Value>::failure(validated.status());
        Value::Object result;
        result["behaviorId"] = definition.value().id;
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> createAgent(const std::string& behaviorId) {
        auto created = world_->createAgent(behaviorId);
        if (!created) return Result<Value>::failure(created.status());
        return Result<Value>::success(Value(static_cast<std::int64_t>(created.value().packed())),
                                      Status::success(StatusCode::Applied));
    }

    Result<Value> destroyAgent(std::int64_t packed) {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        auto destroyed = world_->destroyAgent(handle.value());
        if (!destroyed) return Result<Value>::failure(destroyed.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    bool isAgentStale(std::int64_t packed) const {
        auto handle = parseAgentHandle(packed);
        if (!handle) return true;
        return world_->isStale(handle.value());
    }

    Result<Value> setBlackboardBool(std::int64_t packed, const std::string& key, bool value) {
        return setBlackboard(packed, key, BlackboardValue(value));
    }
    Result<Value> setBlackboardInt(std::int64_t packed, const std::string& key, std::int64_t value) {
        return setBlackboard(packed, key, BlackboardValue(value));
    }
    Result<Value> setBlackboardNumber(std::int64_t packed, const std::string& key, double value) {
        return setBlackboard(packed, key, BlackboardValue(value));
    }
    Result<Value> setBlackboardString(std::int64_t packed, const std::string& key, const std::string& value) {
        return setBlackboard(packed, key, BlackboardValue(value));
    }

    Result<Value> signal(std::int64_t packed, const std::string& signalName) {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        auto signaled = world_->signal(handle.value(), signalName);
        if (!signaled) return Result<Value>::failure(signaled.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    Result<Value> remember(std::int64_t packed, const std::string& subject, const std::string& sense, double confidence,
                           std::int64_t observedTick, std::int64_t forgetAfterTicks, const std::string& payloadJson) {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        if (observedTick < 0 || forgetAfterTicks <= 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "npc_ai remember ticks are invalid", "tick", {},
                                                            "npc_ai.squirrel"));
        PerceptionMemory memory;
        memory.subject          = subject;
        memory.sense            = sense;
        memory.confidence       = confidence;
        memory.observedTick     = static_cast<std::uint64_t>(observedTick);
        memory.forgetAfterTicks = static_cast<std::uint64_t>(forgetAfterTicks);
        memory.payloadJson      = payloadJson.empty() ? "{}" : payloadJson;
        auto remembered         = world_->remember(handle.value(), std::move(memory));
        if (!remembered) return Result<Value>::failure(remembered.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    Result<Value> forget(std::int64_t packed, const std::string& subject, const std::string& sense) {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        auto forgotten = world_->forget(handle.value(), subject, sense);
        if (!forgotten) return Result<Value>::failure(forgotten.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    Result<Value> tick(std::int64_t simulationTick, double deltaSeconds, std::int64_t maxAgents,
                       std::int64_t maxTransitionsPerAgent) {
        auto parsedMaxAgents = parseUInt32Field(maxAgents, "maxAgents");
        if (!parsedMaxAgents) return Result<Value>::failure(parsedMaxAgents.status());
        auto parsedMaxTransitions = parseUInt32Field(maxTransitionsPerAgent, "maxTransitionsPerAgent");
        if (!parsedMaxTransitions) return Result<Value>::failure(parsedMaxTransitions.status());
        if (simulationTick < 0 || !(deltaSeconds >= 0.0))
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "npc_ai tick arguments are invalid", "tick", {},
                                                            "npc_ai.squirrel"));
        TickContext context;
        context.simulationTick         = static_cast<std::uint64_t>(simulationTick);
        context.deltaSeconds           = deltaSeconds;
        context.maxAgents              = parsedMaxAgents.value();
        context.maxTransitionsPerAgent = parsedMaxTransitions.value();
        auto report                    = world_->tick(context);
        if (!report) return Result<Value>::failure(report.status());
        Value::Object result;
        result["agentsUpdated"]              = static_cast<std::int64_t>(report.value().agentsUpdated);
        result["agentsDeferred"]             = static_cast<std::int64_t>(report.value().agentsDeferred);
        result["transitionsApplied"]         = static_cast<std::int64_t>(report.value().transitionsApplied);
        result["transitionBudgetsExhausted"] = static_cast<std::int64_t>(report.value().transitionBudgetsExhausted);
        result["tasksTicked"]                = static_cast<std::int64_t>(report.value().tasksTicked);
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> snapshotValue(std::int64_t packed) const {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        auto snapshot = world_->snapshot(handle.value());
        if (!snapshot) return Result<Value>::failure(snapshot.status());
        Value::Object result;
        result["handle"]      = static_cast<std::int64_t>(snapshot.value().handle.packed());
        result["behaviorId"]  = snapshot.value().behaviorId;
        result["activeState"] = snapshot.value().activeState;
        result["lastTick"]    = static_cast<std::int64_t>(snapshot.value().lastTick);
        Value::Array path;
        for (const auto& state : snapshot.value().activePath) path.emplace_back(state);
        result["activePath"] = std::move(path);
        Value::Object board;
        for (const auto& [key, value] : snapshot.value().blackboard) board[key] = blackboardValueToValue(value);
        result["blackboard"] = std::move(board);
        Value::Array perception;
        for (const auto& memory : snapshot.value().perception) {
            Value::Object item;
            item["subject"]          = memory.subject;
            item["sense"]            = memory.sense;
            item["confidence"]       = memory.confidence;
            item["observedTick"]     = static_cast<std::int64_t>(memory.observedTick);
            item["forgetAfterTicks"] = static_cast<std::int64_t>(memory.forgetAfterTicks);
            item["payloadJson"]      = memory.payloadJson;
            perception.emplace_back(std::move(item));
        }
        result["perception"] = std::move(perception);
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> snapshotJson(std::int64_t packed) const {
        auto snapshot = snapshotValue(packed);
        if (!snapshot) return snapshot;
        auto json = snapshot.value().toJson();
        if (!json) return Result<Value>::failure(json.status());
        return Result<Value>::success(Value(json.value()));
    }

private:
    Result<Value> setBlackboard(std::int64_t packed, const std::string& key, BlackboardValue value) {
        auto handle = parseAgentHandle(packed);
        if (!handle) return Result<Value>::failure(handle.status());
        auto written = world_->setBlackboard(handle.value(), key, std::move(value));
        if (!written) return Result<Value>::failure(written.status());
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    std::unique_ptr<NpcAiWorld> world_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table nullWorld(HSQUIRRELVM vm) {
    return project(vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "npc_ai world must not be null", "world", {},
                                                                "npc_ai.squirrel")));
}

ssq::Table newWorld(HSQUIRRELVM vm, std::int64_t traceCapacity, std::int64_t maxMemoriesPerAgent) {
    auto world  = std::make_unique<ScriptNpcAiWorld>(traceCapacity, maxMemoriesPerAgent);
    auto object = script::makeOwnedSquirrelInstance<ScriptNpcAiWorld>(vm, std::move(world));
    if (!object) return script::projectStatusResult(vm, object.status());
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

Module_IMPL(NpcAi, new NpcAi());

void NpcAi::expose(ssq::Table& table) {
    const HSQUIRRELVM vm    = table.getHandle();
    auto              world = table.addClass<ScriptNpcAiWorld>(
        "NpcAiWorld", std::function<ScriptNpcAiWorld*()>([] { return nullptr; }), true);
    world.addFunc("ownership", [](ScriptNpcAiWorld*) { return std::string("owned"); });
    world.addFunc("registerBehavior", [vm](ScriptNpcAiWorld* self, const std::string& json) {
        return self ? project(vm, self->registerBehavior(json)) : nullWorld(vm);
    });
    world.addFunc("validateBehavior", [vm](ScriptNpcAiWorld* self, const std::string& json) {
        return self ? project(vm, self->validateBehavior(json)) : nullWorld(vm);
    });
    world.addFunc("createAgent", [vm](ScriptNpcAiWorld* self, const std::string& behaviorId) {
        return self ? project(vm, self->createAgent(behaviorId)) : nullWorld(vm);
    });
    world.addFunc("destroyAgent", [vm](ScriptNpcAiWorld* self, std::int64_t agent) {
        return self ? project(vm, self->destroyAgent(agent)) : nullWorld(vm);
    });
    world.addFunc("isAgentStale", [](ScriptNpcAiWorld* self, std::int64_t agent) {
        return !self || self->isAgentStale(agent);
    });
    world.addFunc("setBlackboardBool",
                  [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& key, bool value) {
                      return self ? project(vm, self->setBlackboardBool(agent, key, value)) : nullWorld(vm);
                  });
    world.addFunc("setBlackboardInt",
                  [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& key, std::int64_t value) {
                      return self ? project(vm, self->setBlackboardInt(agent, key, value)) : nullWorld(vm);
                  });
    world.addFunc("setBlackboardNumber",
                  [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& key, float value) {
                      return self ? project(vm, self->setBlackboardNumber(agent, key, value)) : nullWorld(vm);
                  });
    world.addFunc("setBlackboardString",
                  [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& key, const std::string& value) {
                      return self ? project(vm, self->setBlackboardString(agent, key, value)) : nullWorld(vm);
                  });
    world.addFunc("signal", [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& signalName) {
        return self ? project(vm, self->signal(agent, signalName)) : nullWorld(vm);
    });
    world.addFunc("remember", [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& subject,
                                   const std::string& sense, float confidence, std::int64_t observedTick,
                                   std::int64_t forgetAfterTicks, const std::string& payloadJson) {
        return self ? project(vm, self->remember(agent, subject, sense, confidence, observedTick, forgetAfterTicks,
                                                 payloadJson))
                    : nullWorld(vm);
    });
    world.addFunc("forget", [vm](ScriptNpcAiWorld* self, std::int64_t agent, const std::string& subject,
                                 const std::string& sense) {
        return self ? project(vm, self->forget(agent, subject, sense)) : nullWorld(vm);
    });
    world.addFunc("tick", [vm](ScriptNpcAiWorld* self, std::int64_t simulationTick, float deltaSeconds,
                               std::int64_t maxAgents, std::int64_t maxTransitionsPerAgent) {
        return self ? project(vm, self->tick(simulationTick, deltaSeconds, maxAgents, maxTransitionsPerAgent))
                    : nullWorld(vm);
    });
    world.addFunc("snapshot", [vm](ScriptNpcAiWorld* self, std::int64_t agent) {
        return self ? project(vm, self->snapshotValue(agent)) : nullWorld(vm);
    });
    world.addFunc("snapshotJson", [vm](ScriptNpcAiWorld* self, std::int64_t agent) {
        return self ? project(vm, self->snapshotJson(agent)) : nullWorld(vm);
    });

    auto module = table.addClass(name, NpcAi::create, false);
    module.addFunc("getName", &NpcAi::getName);
    module.addFunc("newWorld", [vm](NpcAi*, std::int64_t traceCapacity, std::int64_t maxMemoriesPerAgent) {
        return newWorld(vm, traceCapacity, maxMemoriesPerAgent);
    });
}

void NpcAi::expose(ssq::Class& cls) {
    cls.addFunc("getName", &NpcAi::getName);
    cls.addFunc("newWorld", [vm = cls.getHandle()](NpcAi*, std::int64_t traceCapacity,
                                                   std::int64_t maxMemoriesPerAgent) {
        return newWorld(vm, traceCapacity, maxMemoriesPerAgent);
    });
}

}  // namespace eve::npc_ai
