#include "settlement/SettlementRules.h"

#include <cmath>
#include <limits>
#include <utility>

namespace eve::settlement {
namespace {

template <class T>
eve::Result<T> invalid(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<const eve::Value::Object*> objectWithFields(const eve::Value& value,
                                                         std::initializer_list<std::string_view> fields,
                                                         const std::string& path) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr)
        return invalid<const eve::Value::Object*>(eve::DiagnosticCode::TypeMismatch, "expected object", path);
    if (object->size() != fields.size())
        return invalid<const eve::Value::Object*>(eve::DiagnosticCode::InvalidArgument,
                                                  "object contains missing or unknown fields", path);
    for (const auto field : fields)
        if (!object->contains(std::string(field)))
            return invalid<const eve::Value::Object*>(eve::DiagnosticCode::InvalidArgument,
                                                      "object contains missing or unknown fields", path);
    return eve::Result<const eve::Value::Object*>::success(object);
}

eve::Result<std::string> stringValue(const eve::Value::Object& object, std::string_view field,
                                     const std::string& path) {
    const auto* value = object.at(std::string(field)).getIf<std::string>();
    if (value == nullptr)
        return invalid<std::string>(eve::DiagnosticCode::TypeMismatch, "expected string",
                                    path + "." + std::string(field));
    return eve::Result<std::string>::success(*value);
}

eve::Result<std::int64_t> integerValue(const eve::Value::Object& object, std::string_view field,
                                        const std::string& path) {
    const auto* value = object.at(std::string(field)).getIf<std::int64_t>();
    if (value == nullptr)
        return invalid<std::int64_t>(eve::DiagnosticCode::TypeMismatch, "expected integer",
                                     path + "." + std::string(field));
    return eve::Result<std::int64_t>::success(*value);
}

eve::Result<double> numberValue(const eve::Value::Object& object, std::string_view field, const std::string& path) {
    const auto& value = object.at(std::string(field));
    if (const auto* number = value.getIf<double>()) return eve::Result<double>::success(*number);
    if (const auto* integer = value.getIf<std::int64_t>())
        return eve::Result<double>::success(static_cast<double>(*integer));
    return invalid<double>(eve::DiagnosticCode::TypeMismatch, "expected number",
                           path + "." + std::string(field));
}

eve::Result<std::vector<std::string>> stringsValue(const eve::Value& value, const std::string& path) {
    const auto* array = value.getIf<eve::Value::Array>();
    if (array == nullptr)
        return invalid<std::vector<std::string>>(eve::DiagnosticCode::TypeMismatch, "expected string array", path);
    std::vector<std::string> strings;
    strings.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* item = (*array)[index].getIf<std::string>();
        if (item == nullptr)
            return invalid<std::vector<std::string>>(eve::DiagnosticCode::TypeMismatch, "expected string",
                                                     path + "[" + std::to_string(index) + "]");
        strings.push_back(*item);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(strings));
}

eve::Result<StageKind> stageValue(std::string_view value, std::string path) {
    if (value == "validate") return eve::Result<StageKind>::success(StageKind::Validate);
    if (value == "decision") return eve::Result<StageKind>::success(StageKind::Decision);
    if (value == "source_modifiers") return eve::Result<StageKind>::success(StageKind::SourceModifiers);
    if (value == "target_mitigation") return eve::Result<StageKind>::success(StageKind::TargetMitigation);
    if (value == "armor_shield") return eve::Result<StageKind>::success(StageKind::ArmorShield);
    if (value == "clamp") return eve::Result<StageKind>::success(StageKind::Clamp);
    if (value == "apply") return eve::Result<StageKind>::success(StageKind::Apply);
    if (value == "trigger") return eve::Result<StageKind>::success(StageKind::Trigger);
    if (value == "event") return eve::Result<StageKind>::success(StageKind::Event);
    return invalid<StageKind>(eve::DiagnosticCode::InvalidArgument, "unknown settlement rule stage", std::move(path));
}

eve::Result<RuleOperation> operationValue(std::string_view value, std::string path) {
    if (value == "add") return eve::Result<RuleOperation>::success(RuleOperation::Add);
    if (value == "multiply") return eve::Result<RuleOperation>::success(RuleOperation::Multiply);
    if (value == "resist_flat") return eve::Result<RuleOperation>::success(RuleOperation::ResistFlat);
    if (value == "resist_percent") return eve::Result<RuleOperation>::success(RuleOperation::ResistPercent);
    if (value == "absorb_flat") return eve::Result<RuleOperation>::success(RuleOperation::AbsorbFlat);
    if (value == "clamp_maximum") return eve::Result<RuleOperation>::success(RuleOperation::ClampMaximum);
    if (value == "critical") return eve::Result<RuleOperation>::success(RuleOperation::Critical);
    if (value == "immune") return eve::Result<RuleOperation>::success(RuleOperation::Immune);
    if (value == "lifesteal") return eve::Result<RuleOperation>::success(RuleOperation::Lifesteal);
    if (value == "reflect") return eve::Result<RuleOperation>::success(RuleOperation::Reflect);
    return invalid<RuleOperation>(eve::DiagnosticCode::InvalidArgument, "unknown settlement rule operation",
                                  std::move(path));
}

eve::Result<SettlementRule> decodeRule(const eve::Value& value, const std::string& path) {
    auto object = objectWithFields(value,
                                   {"filter", "id", "operation", "priority", "source", "stacks", "stage", "value",
                                    "valuePerExtraStack", "when"},
                                   path);
    if (!object) return eve::Result<SettlementRule>::failure(object.status());
    auto id       = stringValue(*object.value(), "id", path);
    auto source   = stringValue(*object.value(), "source", path);
    auto stage    = stringValue(*object.value(), "stage", path);
    auto operation = stringValue(*object.value(), "operation", path);
    auto priority = integerValue(*object.value(), "priority", path);
    auto stacks   = integerValue(*object.value(), "stacks", path);
    auto operand  = numberValue(*object.value(), "value", path);
    auto perStack = numberValue(*object.value(), "valuePerExtraStack", path);
    auto when     = stringValue(*object.value(), "when", path);
    if (!id) return eve::Result<SettlementRule>::failure(id.status());
    if (!source) return eve::Result<SettlementRule>::failure(source.status());
    if (!stage) return eve::Result<SettlementRule>::failure(stage.status());
    if (!operation) return eve::Result<SettlementRule>::failure(operation.status());
    if (!priority) return eve::Result<SettlementRule>::failure(priority.status());
    if (!stacks) return eve::Result<SettlementRule>::failure(stacks.status());
    if (!operand) return eve::Result<SettlementRule>::failure(operand.status());
    if (!perStack) return eve::Result<SettlementRule>::failure(perStack.status());
    if (!when) return eve::Result<SettlementRule>::failure(when.status());
    if (priority.value() < std::numeric_limits<int>::min() || priority.value() > std::numeric_limits<int>::max())
        return invalid<SettlementRule>(eve::DiagnosticCode::InvalidArgument, "priority is out of range",
                                       path + ".priority");
    if (stacks.value() <= 0 || stacks.value() > std::numeric_limits<std::uint32_t>::max())
        return invalid<SettlementRule>(eve::DiagnosticCode::InvalidArgument, "stacks is out of range",
                                       path + ".stacks");
    auto decodedStage = stageValue(stage.value(), path + ".stage");
    auto decodedOperation = operationValue(operation.value(), path + ".operation");
    if (!decodedStage) return eve::Result<SettlementRule>::failure(decodedStage.status());
    if (!decodedOperation) return eve::Result<SettlementRule>::failure(decodedOperation.status());

    auto filter = objectWithFields(object.value()->at("filter"), {"excludedTags", "kinds", "requiredTags"},
                                   path + ".filter");
    if (!filter) return eve::Result<SettlementRule>::failure(filter.status());
    auto kinds = stringsValue(filter.value()->at("kinds"), path + ".filter.kinds");
    auto required = stringsValue(filter.value()->at("requiredTags"), path + ".filter.requiredTags");
    auto excluded = stringsValue(filter.value()->at("excludedTags"), path + ".filter.excludedTags");
    if (!kinds) return eve::Result<SettlementRule>::failure(kinds.status());
    if (!required) return eve::Result<SettlementRule>::failure(required.status());
    if (!excluded) return eve::Result<SettlementRule>::failure(excluded.status());

    SettlementRule rule;
    rule.id                     = std::move(id).takeValue();
    rule.source                 = std::move(source).takeValue();
    rule.stage                  = decodedStage.value();
    rule.priority               = static_cast<int>(priority.value());
    rule.operation              = decodedOperation.value();
    rule.value                  = operand.value();
    rule.valuePerExtraStack     = perStack.value();
    rule.stacks                 = static_cast<std::uint32_t>(stacks.value());
    rule.filter.kinds           = std::move(kinds).takeValue();
    rule.filter.requiredTags    = std::move(required).takeValue();
    rule.filter.excludedTags    = std::move(excluded).takeValue();
    rule.when                   = std::move(when).takeValue();
    return eve::Result<SettlementRule>::success(std::move(rule));
}

}  // namespace

eve::Result<SettlementRuleSet> SettlementRuleSet::fromJson(std::string_view json) {
    SettlementRuleSet candidate;
    auto              configured = candidate.configureJson(json);
    if (!configured) return eve::Result<SettlementRuleSet>::failure(configured.status());
    return eve::Result<SettlementRuleSet>::success(std::move(candidate),
                                                    eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SettlementRuleSet::configureJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    auto root = objectWithFields(parsed.value(), {"conditionLanguageVersion", "rules", "schema", "version"},
                                 "document");
    if (!root) return eve::Result<void>::failure(root.status());
    auto schema = stringValue(*root.value(), "schema", "document");
    auto version = integerValue(*root.value(), "version", "document");
    auto conditionVersion = integerValue(*root.value(), "conditionLanguageVersion", "document");
    if (!schema) return eve::Result<void>::failure(schema.status());
    if (!version) return eve::Result<void>::failure(version.status());
    if (!conditionVersion) return eve::Result<void>::failure(conditionVersion.status());
    if (schema.value() != "settlement.rules")
        return invalid<void>(eve::DiagnosticCode::InvalidArgument, "unexpected settlement rules schema",
                             "document.schema");
    if (version.value() != schemaVersion())
        return invalid<void>(eve::DiagnosticCode::UnknownVersion, "unsupported settlement rules version",
                             "document.version");
    if (conditionVersion.value() != 1)
        return invalid<void>(eve::DiagnosticCode::UnknownVersion, "unsupported condition language version",
                             "document.conditionLanguageVersion");
    const auto* encodedRules = root.value()->at("rules").getIf<eve::Value::Array>();
    if (encodedRules == nullptr)
        return invalid<void>(eve::DiagnosticCode::TypeMismatch, "expected rule array", "document.rules");
    std::vector<SettlementRule> rules;
    rules.reserve(encodedRules->size());
    for (std::size_t index = 0; index < encodedRules->size(); ++index) {
        auto rule = decodeRule((*encodedRules)[index], "document.rules[" + std::to_string(index) + "]");
        if (!rule) return eve::Result<void>::failure(rule.status());
        rules.push_back(std::move(rule).takeValue());
    }
    return configure(std::move(rules));
}

}  // namespace eve::settlement
