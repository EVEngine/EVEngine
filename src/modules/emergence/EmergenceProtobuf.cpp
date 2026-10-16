#include "emergence/FactStore.h"
#include "emergence/RuleEngine.h"

#include "common/Diagnostic.h"
#include "common/ProtobufDynamicValue.h"
#include "emergence_runtime.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/json_util.h>

#include <limits>
#include <set>
#include <unordered_set>
#include <utility>

namespace eve::emergence {
namespace {

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
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

eve::Result<std::string> serializeDeterministic(const google::protobuf::MessageLite& message) {
    std::string bytes;
    bool        encoded = false;
    {
        google::protobuf::io::StringOutputStream output(&bytes);
        google::protobuf::io::CodedOutputStream  coded(&output);
        coded.SetSerializationDeterministic(true);
        encoded = message.SerializeToCodedStream(&coded) && !coded.HadError();
    }
    if (!encoded)
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "emergence Protobuf encoding failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

template <class Proto>
eve::Result<void> parseBinary(std::string_view bytes, Proto& message) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid emergence Protobuf bytes"));
    return eve::Result<void>::success();
}

eve::Result<void> invalid(std::string message, std::string path = {}) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message), std::move(path), {}, "emergence"));
}

}  // namespace

eve::Result<void> FactStore::captureMessage(proto::FactStoreSave& save) const {
    save.set_schema("eve.emergence.facts");
    save.set_version(2);
    const auto encodeMap = [](const auto& source, auto* destination) -> eve::Result<void> {
        for (const auto& [key, value] : source) {
            auto encoded = eve::proto_support::encodeDynamicValue(value, (*destination)[key]);
            if (!encoded) return encoded;
        }
        return eve::Result<void>::success();
    };
    if (auto result = encodeMap(values_, save.mutable_values()); !result) return result;
    if (auto result = encodeMap(attributes_, save.mutable_attributes()); !result) return result;
    if (auto result = encodeMap(resources_, save.mutable_resources()); !result) return result;
    if (auto result = encodeMap(states_, save.mutable_states()); !result) return result;
    for (const auto& [key, value] : tags_) (*save.mutable_tags())[key] = value;
    for (const auto& [key, value] : authorities_) (*save.mutable_authorities())[key] = value;
    for (const auto& [key, value] : policies_) {
        auto*      record = &(*save.mutable_policies())[key];
        const auto reason = static_cast<int>(value.reasonCode());
        if (!proto::PolicyReason_IsValid(reason) || value.passed() != (reason == proto::PASSED))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::SerializationError, "invalid policy reason code", "policies", {}, "emergence"));
        record->set_passed(value.passed());
        record->set_reason(static_cast<proto::PolicyReason>(reason));
        auto evidence = eve::proto_support::encodeDynamicValue(value.evidence(), *record->mutable_evidence());
        if (!evidence) return evidence;
        auto details = eve::proto_support::encodeDynamicValue(value.details(), *record->mutable_details());
        if (!details) return details;
    }
    return eve::Result<void>::success();
}

eve::Result<void> FactStore::restoreMessage(const proto::FactStoreSave& save) {
    if (hasUnknownFields(save) || !save.has_schema() || save.schema() != "eve.emergence.facts" || !save.has_version() ||
        save.version() != 2)
        return invalid("invalid fact snapshot header");
    FactStore  candidate;
    const auto decodeMap = [](const auto& source, auto& destination) -> eve::Result<void> {
        for (const auto& [key, value] : source) {
            if (key.empty()) return invalid("fact key must not be empty");
            auto decoded = eve::proto_support::decodeDynamicValue(value);
            if (!decoded) return eve::Result<void>::failure(decoded.status());
            destination.emplace(key, std::move(decoded).takeValue());
        }
        return eve::Result<void>::success();
    };
    if (auto result = decodeMap(save.values(), candidate.values_); !result) return result;
    if (auto result = decodeMap(save.attributes(), candidate.attributes_); !result) return result;
    if (auto result = decodeMap(save.resources(), candidate.resources_); !result) return result;
    if (auto result = decodeMap(save.states(), candidate.states_); !result) return result;
    for (const auto& [key, value] : save.tags()) {
        if (key.empty()) return invalid("tag key must not be empty", "tags");
        candidate.tags_.emplace(key, value);
    }
    for (const auto& [key, value] : save.authorities()) {
        if (key.empty()) return invalid("authority key must not be empty", "authorities");
        candidate.authorities_.emplace(key, value);
    }
    for (const auto& [key, value] : save.policies()) {
        if (key.empty() || !value.has_passed() || !value.has_reason() || !value.has_evidence() ||
            !value.has_details() || !proto::PolicyReason_IsValid(static_cast<int>(value.reason())) ||
            value.passed() != (value.reason() == proto::PASSED))
            return invalid("invalid policy result", "policies");
        auto evidence = eve::proto_support::decodeDynamicValue(value.evidence());
        if (!evidence) return eve::Result<void>::failure(evidence.status());
        auto details = eve::proto_support::decodeDynamicValue(value.details());
        if (!details) return eve::Result<void>::failure(details.status());
        auto result =
            value.passed()
                ? condition::ConditionResult::success(std::move(evidence).takeValue(), std::move(details).takeValue())
                : condition::ConditionResult::failed(static_cast<condition::ConditionReasonCode>(value.reason()),
                                                    std::move(evidence).takeValue(), std::move(details).takeValue());
        candidate.policies_.emplace(key, std::move(result));
    }
    *this = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> FactStore::snapshotBinary() const {
    proto::FactStoreSave save;
    auto                 captured = captureMessage(save);
    if (!captured) return eve::Result<std::string>::failure(captured.status());
    return serializeDeterministic(save);
}

eve::Result<void> FactStore::restoreBinary(std::string_view bytes) {
    proto::FactStoreSave save;
    auto                 parsed = parseBinary(bytes, save);
    if (!parsed) return parsed;
    return restoreMessage(save);
}

std::string FactStore::snapshotJson() const {
    proto::FactStoreSave save;
    if (!captureMessage(save)) return {};
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(save, &json).ok()) return {};
    return json;
}

eve::Result<void> RuleEngine::captureMessage(proto::RuleEngineSave& save) const {
    if (runtime_.size() != rules_.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                 "rule runtime size differs from catalogue", "rules",
                                                                 {}, "emergence"));
    save.set_schema("eve.emergence.runtime");
    save.set_version(2);
    auto facts = facts_.captureMessage(*save.mutable_facts());
    if (!facts) return facts;
    for (std::size_t index = 0; index < rules_.size(); ++index) {
        auto* latch = save.add_rules();
        latch->set_id(rules_[index].id);
        latch->set_passed(runtime_[index].passed);
        latch->set_fired(runtime_[index].fired);
        latch->set_disabled(runtime_[index].disabled);
        latch->set_cooldown_until(runtime_[index].cooldownUntil);
    }
    return eve::Result<void>::success();
}

eve::Result<void> RuleEngine::restoreMessage(const proto::RuleEngineSave& save) {
    if (hasUnknownFields(save) || !save.has_schema() || save.schema() != "eve.emergence.runtime" ||
        !save.has_version() || save.version() != 2 || !save.has_facts() ||
        static_cast<std::size_t>(save.rules_size()) != rules_.size())
        return invalid("invalid rule runtime snapshot header");
    FactStore nextFacts;
    auto      restoredFacts = nextFacts.restoreMessage(save.facts());
    if (!restoredFacts) return restoredFacts;
    std::vector<RuleRuntime>        nextRuntime(runtime_.size());
    std::unordered_set<std::string> seenIds;
    for (const auto& latch : save.rules()) {
        if (!latch.has_id() || latch.id().empty() || !latch.has_passed() || !latch.has_fired() ||
            !latch.has_disabled() || !latch.has_cooldown_until() || !seenIds.insert(latch.id()).second)
            return invalid("invalid rule latch", "rules");
        auto index = idToIndex_.find(latch.id());
        if (index == idToIndex_.end()) return invalid("rule id missing from catalogue", "rules.id");
        auto& runtime         = nextRuntime[index->second];
        runtime.passed        = latch.passed();
        runtime.fired         = latch.fired();
        runtime.disabled      = latch.disabled();
        runtime.cooldownUntil = latch.cooldown_until();
    }
    facts_   = std::move(nextFacts);
    runtime_ = std::move(nextRuntime);
    dirty_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> RuleEngine::snapshotBinary() const {
    proto::RuleEngineSave save;
    auto                  captured = captureMessage(save);
    if (!captured) return eve::Result<std::string>::failure(captured.status());
    return serializeDeterministic(save);
}

eve::Result<void> RuleEngine::restoreBinary(std::string_view bytes) {
    proto::RuleEngineSave save;
    auto                  parsed = parseBinary(bytes, save);
    if (!parsed) return parsed;
    return restoreMessage(save);
}

std::string RuleEngine::snapshotJson() const {
    proto::RuleEngineSave save;
    if (!captureMessage(save)) return {};
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(save, &json).ok()) return {};
    return json;
}

}  // namespace eve::emergence
