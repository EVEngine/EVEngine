#include "settlement/SettlementRules.h"

#include "effects/EffectContainer.h"

#include <cmath>
#include <string>
#include <utility>

namespace eve::settlement {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

eve::Result<std::string> stringField(const eve::Value::Object& object, std::string_view name, std::string path,
                                     bool required = false) {
    const auto* value = field(object, name);
    if (value == nullptr) {
        if (required) return invalid<std::string>("required string field is missing", std::move(path));
        return eve::Result<std::string>::success({});
    }
    const auto* string = value->getIf<std::string>();
    if (string == nullptr) return invalid<std::string>("field must be a string", std::move(path));
    return eve::Result<std::string>::success(*string);
}

eve::Result<double> numberField(const eve::Value::Object& object, std::string_view name, double fallback,
                                std::string path) {
    const auto* value = field(object, name);
    if (value == nullptr) return eve::Result<double>::success(fallback);
    double number = 0.0;
    if (const auto* real = value->getIf<double>())
        number = *real;
    else if (const auto* integer = value->getIf<std::int64_t>())
        number = static_cast<double>(*integer);
    else
        return invalid<double>("field must be a number", std::move(path));
    if (!std::isfinite(number)) return invalid<double>("field must be finite", std::move(path));
    return eve::Result<double>::success(number);
}

eve::Result<std::vector<std::string>> stringsField(const eve::Value::Object& object, std::string_view name,
                                                   std::string path) {
    const auto* value = field(object, name);
    if (value == nullptr) return eve::Result<std::vector<std::string>>::success({});
    const auto* array = value->getIf<eve::Value::Array>();
    if (array == nullptr) return invalid<std::vector<std::string>>("field must be an array of strings", path);
    std::vector<std::string> result;
    result.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* string = (*array)[index].getIf<std::string>();
        if (string == nullptr)
            return invalid<std::vector<std::string>>("array item must be a string",
                                                     path + "[" + std::to_string(index) + "]");
        result.push_back(*string);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(result));
}

eve::Result<StageKind> parseStage(std::string_view value, std::string path) {
    if (value == "decision") return eve::Result<StageKind>::success(StageKind::Decision);
    if (value == "source_modifiers") return eve::Result<StageKind>::success(StageKind::SourceModifiers);
    if (value == "target_mitigation") return eve::Result<StageKind>::success(StageKind::TargetMitigation);
    if (value == "armor_shield") return eve::Result<StageKind>::success(StageKind::ArmorShield);
    if (value == "clamp") return eve::Result<StageKind>::success(StageKind::Clamp);
    return invalid<StageKind>("unknown settlement rule stage", std::move(path));
}

eve::Result<RuleOperation> parseOperation(std::string_view value, std::string path) {
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
    return invalid<RuleOperation>("unknown settlement rule operation", std::move(path));
}

eve::Result<eve::Value::Object> ruleObject(const effects::EffectInstance& effect, std::string path) {
    const auto* encoded = field(effect.payload.object(), "settlement.rule");
    if (encoded == nullptr) return eve::Result<eve::Value::Object>::success({});
    if (const auto* object = encoded->getIf<eve::Value::Object>())
        return eve::Result<eve::Value::Object>::success(*object);
    if (const auto* json = encoded->getIf<std::string>()) {
        auto parsed = eve::Value::fromJson(*json);
        if (!parsed) return eve::Result<eve::Value::Object>::failure(parsed.status());
        auto        value  = std::move(parsed).takeValue();
        const auto* object = value.getIf<eve::Value::Object>();
        if (object == nullptr) return invalid<eve::Value::Object>("settlement.rule JSON must be an object", path);
        return eve::Result<eve::Value::Object>::success(*object);
    }
    return invalid<eve::Value::Object>("settlement.rule must be an object or JSON object string", std::move(path));
}

}  // namespace

eve::Result<SettlementRuleSet> projectEffectRules(const effects::EffectContainer& effects, std::string_view scope) {
    if (scope.empty()) return invalid<SettlementRuleSet>("effect rule scope must not be empty", "scope");
    std::vector<SettlementRule> rules;
    for (int index = 0; index < effects.effectCount(); ++index) {
        const auto* effect = effects.effectAt(index);
        if (effect == nullptr)
            return invalid<SettlementRuleSet>("effect container returned an empty active slot",
                                              "effects[" + std::to_string(index) + "]");
        if (!effect->payload.has("settlement.rule")) continue;
        const auto path         = "effects[" + std::to_string(index) + "].payload.settlement.rule";
        auto       objectResult = ruleObject(*effect, path);
        if (!objectResult) return eve::Result<SettlementRuleSet>::failure(objectResult.status());
        auto object = std::move(objectResult).takeValue();

        auto stageName = stringField(object, "stage", path + ".stage", true);
        if (!stageName) return eve::Result<SettlementRuleSet>::failure(stageName.status());
        auto operationName = stringField(object, "operation", path + ".operation", true);
        if (!operationName) return eve::Result<SettlementRuleSet>::failure(operationName.status());
        auto stage = parseStage(stageName.value(), path + ".stage");
        if (!stage) return eve::Result<SettlementRuleSet>::failure(stage.status());
        auto operation = parseOperation(operationName.value(), path + ".operation");
        if (!operation) return eve::Result<SettlementRuleSet>::failure(operation.status());
        auto value = numberField(object, "value", effect->magnitude, path + ".value");
        if (!value) return eve::Result<SettlementRuleSet>::failure(value.status());
        auto perStack = numberField(object, "value_per_extra_stack", 0.0, path + ".value_per_extra_stack");
        if (!perStack) return eve::Result<SettlementRuleSet>::failure(perStack.status());
        auto when = stringField(object, "when", path + ".when");
        if (!when) return eve::Result<SettlementRuleSet>::failure(when.status());
        auto kinds = stringsField(object, "kinds", path + ".kinds");
        if (!kinds) return eve::Result<SettlementRuleSet>::failure(kinds.status());
        auto required = stringsField(object, "required_tags", path + ".required_tags");
        if (!required) return eve::Result<SettlementRuleSet>::failure(required.status());
        auto excluded = stringsField(object, "excluded_tags", path + ".excluded_tags");
        if (!excluded) return eve::Result<SettlementRuleSet>::failure(excluded.status());

        SettlementRule rule;
        rule.id                  = std::string(scope) + "." + effect->id;
        rule.source              = effect->source.empty() ? effect->type : effect->source;
        rule.stage               = stage.value();
        rule.priority            = effect->priority;
        rule.operation           = operation.value();
        rule.value               = value.value();
        rule.valuePerExtraStack  = perStack.value();
        rule.stacks              = effect->stackCount;
        rule.filter.kinds        = std::move(kinds).takeValue();
        rule.filter.requiredTags = std::move(required).takeValue();
        rule.filter.excludedTags = std::move(excluded).takeValue();
        rule.when                = std::move(when).takeValue();
        rules.push_back(std::move(rule));
    }
    SettlementRuleSet result;
    auto              configured = result.configure(std::move(rules));
    if (!configured) return eve::Result<SettlementRuleSet>::failure(configured.status());
    return eve::Result<SettlementRuleSet>::success(std::move(result));
}

}  // namespace eve::settlement
