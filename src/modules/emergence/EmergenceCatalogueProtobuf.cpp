#include "emergence/RuleEngine.h"

#include "common/ConditionKeys.h"
#include "common/Diagnostic.h"
#include "common/ProtobufDynamicValue.h"
#include "emergence_catalogue.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/json_util.h>

#include <limits>
#include <utility>

namespace eve::emergence {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message), std::move(path), {}, "emergence"));
}

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

eve::Result<void> encodeCondition(const condition::Condition& node, proto::Condition& record) {
    if (!node.isValid())
        return invalid<void>("invalid rule condition", "condition");
    using K = condition::ConditionKind;
    switch (node.kind()) {
        case K::All:
        case K::Any:
        case K::Not: {
            record.set_kind(node.kind() == K::All ? proto::ALL : node.kind() == K::Any ? proto::ANY : proto::NOT);
            for (const auto& child : node.children()) {
                auto encoded = encodeCondition(child, *record.add_children());
                if (!encoded) return encoded;
            }
            return eve::Result<void>::success();
        }
        case K::Compare: {
            if (!proto::CompareOperator_IsValid(static_cast<int>(node.compareOperator())))
                return invalid<void>("invalid compare operator", "condition.operator");
            record.set_kind(proto::COMPARE);
            record.set_key(node.key());
            record.set_operator_(static_cast<proto::CompareOperator>(node.compareOperator()));
            if (node.compareOperator() == condition::CompareOperator::Exists)
                return eve::Result<void>::success();
            return eve::proto_support::encodeDynamicValue(node.expected(), *record.mutable_expected());
        }
        case K::PolicyCall: {
            record.set_kind(proto::POLICY_CALL);
            record.set_key(node.key());
            auto encoded = eve::proto_support::encodeDynamicValue(node.arguments(), *record.mutable_arguments());
            if (!encoded) return encoded;
            if (const auto& declaration = node.scriptDeclaration()) {
                if (!proto::DeterminismLevel_IsValid(static_cast<int>(declaration->determinism)))
                    return invalid<void>("invalid determinism level", "condition.scriptDeclaration.determinism");
                auto* metadata = record.mutable_script_declaration();
                metadata->set_name(declaration->name);
                metadata->set_determinism(static_cast<proto::DeterminismLevel>(declaration->determinism));
                for (const auto& dependency : declaration->dependencies) metadata->add_dependencies(dependency);
            }
            return eve::Result<void>::success();
        }
    }
    return invalid<void>("unsupported rule condition", "condition.kind");
}

eve::Result<condition::Condition> decodeCondition(const proto::Condition& record) {
    if (!record.has_kind() || !proto::ConditionKind_IsValid(static_cast<int>(record.kind())))
        return invalid<condition::Condition>("invalid condition kind", "condition.kind");
    const auto kind    = record.kind();
    const bool hasLeaf = record.has_key() || record.has_operator_() || record.has_expected() ||
                         record.has_arguments() || record.has_script_declaration();
    if (kind == proto::ALL || kind == proto::ANY || kind == proto::NOT) {
        if (hasLeaf || (kind == proto::NOT && record.children_size() != 1))
            return invalid<condition::Condition>("invalid logical condition fields", "condition");
        std::vector<condition::Condition> children;
        children.reserve(record.children_size());
        for (const auto& child : record.children()) {
            auto decoded = decodeCondition(child);
            if (!decoded) return eve::Result<condition::Condition>::failure(decoded.status());
            children.push_back(std::move(decoded).takeValue());
        }
        if (kind == proto::ALL)
            return eve::Result<condition::Condition>::success(condition::Condition::all(std::move(children)));
        if (kind == proto::ANY)
            return eve::Result<condition::Condition>::success(condition::Condition::any(std::move(children)));
        return eve::Result<condition::Condition>::success(condition::Condition::not_(std::move(children.front())));
    }
    if (record.children_size() != 0 || !record.has_key() || record.key().empty())
        return invalid<condition::Condition>("invalid leaf condition fields", "condition");
    if (kind == proto::COMPARE) {
        if (!record.has_operator_() || !proto::CompareOperator_IsValid(static_cast<int>(record.operator_())) ||
            record.has_expected() != (record.operator_() != proto::EXISTS) || record.has_arguments() ||
            record.has_script_declaration())
            return invalid<condition::Condition>("invalid compare condition", "condition");
        if (record.operator_() == proto::EXISTS)
            return eve::Result<condition::Condition>::success(
                condition::Condition::compare(record.key(), condition::CompareOperator::Exists));
        auto expected = eve::proto_support::decodeDynamicValue(record.expected());
        if (!expected) return eve::Result<condition::Condition>::failure(expected.status());
        return eve::Result<condition::Condition>::success(condition::Condition::compare(
            record.key(), static_cast<condition::CompareOperator>(record.operator_()), std::move(expected).takeValue()));
    }
    if (kind == proto::STATE_EQUALS) {
        if (record.has_operator_() || !record.has_expected() || record.has_arguments() ||
            record.has_script_declaration())
            return invalid<condition::Condition>("invalid state condition", "condition");
        auto expected = eve::proto_support::decodeDynamicValue(record.expected());
        if (!expected) return eve::Result<condition::Condition>::failure(expected.status());
        return eve::Result<condition::Condition>::success(
            condition::stateEquals(record.key(), std::move(expected).takeValue()));
    }
    if (kind == proto::POLICY_CALL) {
        if (record.has_operator_() || record.has_expected() || !record.has_arguments())
            return invalid<condition::Condition>("invalid policy condition", "condition");
        auto arguments = eve::proto_support::decodeDynamicValue(record.arguments());
        if (!arguments) return eve::Result<condition::Condition>::failure(arguments.status());
        std::optional<condition::ScriptConditionDeclaration> declaration;
        if (record.has_script_declaration()) {
            const auto& metadata = record.script_declaration();
            if (!metadata.has_name() || !metadata.has_determinism() ||
                !proto::DeterminismLevel_IsValid(static_cast<int>(metadata.determinism())))
                return invalid<condition::Condition>("invalid script declaration", "condition.scriptDeclaration");
            condition::ScriptConditionDeclaration parsed;
            parsed.name        = metadata.name();
            parsed.determinism = static_cast<condition::DeterminismLevel>(metadata.determinism());
            for (const auto& dependency : metadata.dependencies()) {
                if (dependency.empty())
                    return invalid<condition::Condition>("empty script dependency", "condition.scriptDeclaration");
                parsed.dependencies.push_back(dependency);
            }
            declaration = std::move(parsed);
        }
        return eve::Result<condition::Condition>::success(
            condition::Condition::policyCall(record.key(), std::move(arguments).takeValue(), std::move(declaration)));
    }
    if (record.has_operator_() || record.has_expected() || record.has_arguments() || record.has_script_declaration())
        return invalid<condition::Condition>("unexpected leaf condition field", "condition");
    switch (kind) {
        case proto::HAS_TAG:
            return eve::Result<condition::Condition>::success(condition::tagPresent(record.key()));
        case proto::HAS_ATTRIBUTE:
            return eve::Result<condition::Condition>::success(condition::attributeExists(record.key()));
        case proto::HAS_RESOURCE:
            return eve::Result<condition::Condition>::success(condition::resourceExists(record.key()));
        case proto::AUTHORITY_CHECK:
            return eve::Result<condition::Condition>::success(condition::authorityAllow(record.key()));
        default: return invalid<condition::Condition>("unsupported condition kind", "condition.kind");
    }
}

eve::Result<void> encodeRule(const RuleDefinition& rule, proto::RuleDefinition& record) {
    if (!proto::RuleFireMode_IsValid(static_cast<int>(rule.fireMode)))
        return invalid<void>("invalid rule fire mode", "rules.fireMode");
    record.set_id(rule.id);
    record.set_priority(rule.priority);
    record.set_fire_mode(static_cast<proto::RuleFireMode>(rule.fireMode));
    record.set_once(rule.once);
    record.set_cooldown_ticks(rule.cooldownTicks);
    auto condition = encodeCondition(rule.condition, *record.mutable_condition());
    if (!condition) return condition;
    for (const auto& key : rule.watchKeys) record.add_watch_keys(key);
    for (const auto& action : rule.actions) {
        auto* encoded = record.add_actions();
        encoded->set_kind(action.kind);
        auto args = eve::proto_support::encodeDynamicValue(action.args, *encoded->mutable_args());
        if (!args) return args;
    }
    return eve::Result<void>::success();
}

eve::Result<RuleDefinition> decodeRule(const proto::RuleDefinition& record) {
    if (!record.has_id() || record.id().empty() || !record.has_priority() || !record.has_fire_mode() ||
        !proto::RuleFireMode_IsValid(static_cast<int>(record.fire_mode())) || !record.has_once() ||
        !record.has_cooldown_ticks() || !record.has_condition())
        return invalid<RuleDefinition>("invalid rule definition", "rules");
    RuleDefinition rule;
    rule.id            = record.id();
    rule.priority      = record.priority();
    rule.fireMode      = static_cast<FireMode>(record.fire_mode());
    rule.once          = record.once();
    rule.cooldownTicks = record.cooldown_ticks();
    auto condition     = decodeCondition(record.condition());
    if (!condition) return eve::Result<RuleDefinition>::failure(condition.status());
    rule.condition = std::move(condition).takeValue();
    for (const auto& key : record.watch_keys()) {
        if (key.empty()) return invalid<RuleDefinition>("empty watch key", "rules.watchKeys");
        rule.watchKeys.push_back(key);
    }
    for (const auto& action : record.actions()) {
        if (!action.has_kind() || action.kind().empty() || !action.has_args())
            return invalid<RuleDefinition>("invalid action", "rules.actions");
        auto args = eve::proto_support::decodeDynamicValue(action.args());
        if (!args) return eve::Result<RuleDefinition>::failure(args.status());
        rule.actions.push_back(EmergenceAction{action.kind(), std::move(args).takeValue()});
    }
    return eve::Result<RuleDefinition>::success(std::move(rule));
}

}  // namespace

eve::Result<void> RuleEngine::captureCatalogueMessage(proto::RuleCatalogueSave& save) const {
    save.set_schema("eve.emergence.rules");
    save.set_version(2);
    for (const auto& rule : rules_) {
        auto encoded = encodeRule(rule, *save.add_rules());
        if (!encoded) return encoded;
    }
    return eve::Result<void>::success();
}

eve::Result<int> RuleEngine::replaceCatalogueMessage(const proto::RuleCatalogueSave& save) {
    if (hasUnknownFields(save) || !save.has_schema() || save.schema() != "eve.emergence.rules" || !save.has_version() ||
        save.version() != 2)
        return invalid<int>("invalid rule catalogue header");
    std::vector<RuleDefinition> rules;
    rules.reserve(save.rules_size());
    for (const auto& record : save.rules()) {
        auto decoded = decodeRule(record);
        if (!decoded) return eve::Result<int>::failure(decoded.status());
        rules.push_back(std::move(decoded).takeValue());
    }
    return replaceCatalogue(std::move(rules));
}

eve::Result<std::string> RuleEngine::snapshotCatalogueBinary() const {
    proto::RuleCatalogueSave save;
    auto                     captured = captureCatalogueMessage(save);
    if (!captured) return eve::Result<std::string>::failure(captured.status());
    std::string bytes;
    bool        encoded = false;
    {
        google::protobuf::io::StringOutputStream output(&bytes);
        google::protobuf::io::CodedOutputStream  coded(&output);
        coded.SetSerializationDeterministic(true);
        encoded = save.SerializeToCodedStream(&coded) && !coded.HadError();
    }
    if (!encoded)
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "rule catalogue encoding failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> RuleEngine::snapshotCatalogueJson() const {
    proto::RuleCatalogueSave save;
    auto                     captured = captureCatalogueMessage(save);
    if (!captured) return eve::Result<std::string>::failure(captured.status());
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(save, &json).ok())
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "rule catalogue JSON encoding failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<int> RuleEngine::replaceCatalogueBinary(std::string_view bytes) {
    proto::RuleCatalogueSave save;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return invalid<int>("invalid rule catalogue Protobuf bytes");
    return replaceCatalogueMessage(save);
}

}  // namespace eve::emergence
