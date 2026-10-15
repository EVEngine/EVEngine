#include "agent/AgentPersistence.h"

#include "agent_evidence.pb.h"

#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace eve::agent {
namespace {

constexpr std::uint32_t formatVersion = 2;

template <typename T>
Result<T> invalid(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "agent.persistence"));
}

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (reflection->GetUnknownFields(message).field_count() != 0) return true;
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index)
                if (hasUnknownFields(reflection->GetRepeatedMessage(message, field, index))) return true;
        } else if (hasUnknownFields(reflection->GetMessage(message, field))) {
            return true;
        }
    }
    return false;
}

template <typename T>
Result<T> parseBinary(std::string_view bytes) {
    T message;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return invalid<T>(DiagnosticCode::ParseError, "invalid agent Protobuf bytes");
    if (hasUnknownFields(message))
        return invalid<T>(DiagnosticCode::UnknownVersion, "agent Protobuf contains unknown fields");
    return Result<T>::success(std::move(message));
}

template <typename T>
Result<T> parseJson(std::string_view json) {
    T          message;
    const auto status = google::protobuf::util::JsonStringToMessage(std::string(json), &message);
    if (!status.ok()) return invalid<T>(DiagnosticCode::ParseError, "invalid agent Protobuf JSON");
    return Result<T>::success(std::move(message));
}

template <typename T>
Result<std::string> writeBinary(const T& message) {
    std::string bytes;
    if (!message.SerializeToString(&bytes))
        return invalid<std::string>(DiagnosticCode::SerializationError, "agent Protobuf serialization failed");
    return Result<std::string>::success(std::move(bytes));
}

template <typename T>
Result<std::string> writeJson(const T& message) {
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(message, &json).ok())
        return invalid<std::string>(DiagnosticCode::SerializationError, "agent Protobuf JSON serialization failed");
    return Result<std::string>::success(std::move(json));
}

proto::PolicyDocument encodePolicy(const Policy& policy) {
    proto::PolicyDocument message;
    message.set_schema(policy.schemaId);
    message.set_version(formatVersion);
    message.set_feature_count(policy.featureCount);
    message.set_action_count(policy.actionCount);
    message.set_hidden_width(policy.hiddenWidth);
    for (double weight : policy.weights) message.add_weights(weight);
    return message;
}

Result<Policy> decodePolicy(const proto::PolicyDocument& message) {
    if (!message.has_schema() || message.schema() != "evengine.agent.policy" || !message.has_version() ||
        message.version() != formatVersion || !message.has_feature_count() || !message.has_action_count() ||
        !message.has_hidden_width())
        return invalid<Policy>(DiagnosticCode::UnknownVersion, "unsupported agent policy schema or version");
    Policy policy;
    policy.featureCount = message.feature_count();
    policy.actionCount  = message.action_count();
    policy.hiddenWidth  = message.hidden_width();
    policy.weights.assign(message.weights().begin(), message.weights().end());
    auto valid = validatePolicy(policy);
    if (!valid.ok()) return Result<Policy>::failure(valid.status());
    return Result<Policy>::success(std::move(policy));
}

void encodeObservation(const Observation& source, proto::Observation* target) {
    for (float feature : source.features) target->add_features(feature);
    for (std::uint32_t action : source.legalActions) target->add_legal_actions(action);
    for (const auto& key : source.coverage) target->add_coverage(key);
    target->set_reward(source.reward);
    switch (source.outcome) {
        case Outcome::Running: target->set_outcome(proto::RUNNING); break;
        case Outcome::Success: target->set_outcome(proto::SUCCESS); break;
        case Outcome::Failure: target->set_outcome(proto::FAILURE); break;
    }
    target->set_finding(source.finding);
}

Result<Observation> decodeObservation(const proto::Observation& source) {
    if (!source.has_reward() || !source.has_outcome() || source.outcome() == proto::OUTCOME_UNSPECIFIED ||
        !proto::Outcome_IsValid(source.outcome()) || !source.has_finding())
        return invalid<Observation>(DiagnosticCode::ParseError, "incomplete agent observation");
    Observation target;
    target.features.assign(source.features().begin(), source.features().end());
    target.legalActions.assign(source.legal_actions().begin(), source.legal_actions().end());
    target.coverage.assign(source.coverage().begin(), source.coverage().end());
    target.reward  = source.reward();
    target.outcome = source.outcome() == proto::RUNNING   ? Outcome::Running
                     : source.outcome() == proto::SUCCESS ? Outcome::Success
                                                          : Outcome::Failure;
    target.finding = source.finding();
    return Result<Observation>::success(std::move(target));
}

proto::TraceDocument encodeTrace(const Trace& trace) {
    proto::TraceDocument message;
    message.set_schema(trace.schemaId);
    message.set_version(formatVersion);
    message.set_environment_seed(trace.environmentSeed);
    message.set_dt(trace.dt);
    encodeObservation(trace.initial, message.mutable_initial());
    for (const auto& step : trace.steps) {
        auto* saved = message.add_steps();
        saved->set_action(step.action);
        encodeObservation(step.observation, saved->mutable_observation());
    }
    return message;
}

Result<Trace> decodeTrace(const proto::TraceDocument& message) {
    if (!message.has_schema() || message.schema() != "evengine.agent.trace" || !message.has_version() ||
        message.version() != formatVersion || !message.has_environment_seed() || !message.has_dt() ||
        !message.has_initial() || message.steps_size() > 1024)
        return invalid<Trace>(DiagnosticCode::UnknownVersion, "unsupported agent trace schema or version");
    Trace trace;
    trace.environmentSeed = message.environment_seed();
    trace.dt              = message.dt();
    auto initial          = decodeObservation(message.initial());
    if (!initial.ok()) return Result<Trace>::failure(initial.status());
    trace.initial = std::move(initial).takeValue();
    for (const auto& saved : message.steps()) {
        if (!saved.has_action() || !saved.has_observation())
            return invalid<Trace>(DiagnosticCode::ParseError, "incomplete agent trace step");
        auto observation = decodeObservation(saved.observation());
        if (!observation.ok()) return Result<Trace>::failure(observation.status());
        trace.steps.push_back({saved.action(), std::move(observation).takeValue()});
    }
    auto valid = validateTrace(trace);
    if (!valid.ok()) return Result<Trace>::failure(valid.status());
    return Result<Trace>::success(std::move(trace));
}

}  // namespace

Result<std::string> policyToBinary(const Policy& policy) {
    auto valid = validatePolicy(policy);
    if (!valid.ok()) return Result<std::string>::failure(valid.status());
    return writeBinary(encodePolicy(policy));
}

Result<Policy> policyFromBinary(std::string_view bytes) {
    auto parsed = parseBinary<proto::PolicyDocument>(bytes);
    if (!parsed.ok()) return Result<Policy>::failure(parsed.status());
    return decodePolicy(parsed.value());
}

Result<std::string> policyToJson(const Policy& policy) {
    auto valid = validatePolicy(policy);
    if (!valid.ok()) return Result<std::string>::failure(valid.status());
    return writeJson(encodePolicy(policy));
}

Result<Policy> policyFromJson(std::string_view json) {
    auto parsed = parseJson<proto::PolicyDocument>(json);
    if (!parsed.ok()) return Result<Policy>::failure(parsed.status());
    return decodePolicy(parsed.value());
}

Result<std::string> traceToBinary(const Trace& trace) {
    auto valid = validateTrace(trace);
    if (!valid.ok()) return Result<std::string>::failure(valid.status());
    return writeBinary(encodeTrace(trace));
}

Result<Trace> traceFromBinary(std::string_view bytes) {
    auto parsed = parseBinary<proto::TraceDocument>(bytes);
    if (!parsed.ok()) return Result<Trace>::failure(parsed.status());
    return decodeTrace(parsed.value());
}

Result<std::string> traceToJson(const Trace& trace) {
    auto valid = validateTrace(trace);
    if (!valid.ok()) return Result<std::string>::failure(valid.status());
    return writeJson(encodeTrace(trace));
}

Result<Trace> traceFromJson(std::string_view json) {
    auto parsed = parseJson<proto::TraceDocument>(json);
    if (!parsed.ok()) return Result<Trace>::failure(parsed.status());
    return decodeTrace(parsed.value());
}

}  // namespace eve::agent
