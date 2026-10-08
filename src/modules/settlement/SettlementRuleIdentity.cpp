#include "settlement/SettlementRules.h"

#include <algorithm>
#include <utility>

namespace eve::settlement {
namespace {

const char* ruleOperationName(RuleOperation operation) noexcept {
    switch (operation) {
        case RuleOperation::Add: return "add";
        case RuleOperation::Multiply: return "multiply";
        case RuleOperation::ResistFlat: return "resist_flat";
        case RuleOperation::ResistPercent: return "resist_percent";
        case RuleOperation::AbsorbFlat: return "absorb_flat";
        case RuleOperation::ClampMaximum: return "clamp_maximum";
        case RuleOperation::Critical: return "critical";
        case RuleOperation::Immune: return "immune";
        case RuleOperation::Lifesteal: return "lifesteal";
        case RuleOperation::Reflect: return "reflect";
    }
    return "unknown";
}

eve::Value::Array canonicalStrings(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    eve::Value::Array encoded;
    encoded.reserve(values.size());
    for (auto& value : values) encoded.emplace_back(std::move(value));
    return encoded;
}

eve::Value encodeRule(const SettlementRule& rule) {
    return eve::Value::Object{
        {"filter",
         eve::Value::Object{{"excludedTags", canonicalStrings(rule.filter.excludedTags)},
                            {"kinds", canonicalStrings(rule.filter.kinds)},
                            {"requiredTags", canonicalStrings(rule.filter.requiredTags)}}},
        {"id", rule.id},
        {"operation", ruleOperationName(rule.operation)},
        {"priority", static_cast<std::int64_t>(rule.priority)},
        {"source", rule.source},
        {"stacks", static_cast<std::int64_t>(rule.stacks)},
        {"stage", stageKindName(rule.stage)},
        {"value", rule.value},
        {"valuePerExtraStack", rule.valuePerExtraStack},
        {"when", rule.when},
    };
}

}  // namespace

eve::Result<std::string> SettlementRuleSet::canonicalJson() const {
    std::vector<const SettlementRule*> ordered;
    ordered.reserve(rules_.size());
    for (const auto& rule : rules_) ordered.push_back(&rule);
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        const auto leftStage  = static_cast<std::uint8_t>(left->stage);
        const auto rightStage = static_cast<std::uint8_t>(right->stage);
        if (leftStage != rightStage) return leftStage < rightStage;
        if (left->priority != right->priority) return left->priority < right->priority;
        return left->id < right->id;
    });

    eve::Value::Array rules;
    rules.reserve(ordered.size());
    for (const auto* rule : ordered) rules.push_back(encodeRule(*rule));
    return eve::Value(eve::Value::Object{
                          {"conditionLanguageVersion", 1},
                          {"rules", std::move(rules)},
                          {"schema", "settlement.rules"},
                          {"version", static_cast<std::int64_t>(schemaVersion())},
                      })
        .toJson();
}

eve::Result<eve::ContentId> SettlementRuleSet::digest(const eve::SnapshotHashProvider& hashProvider) const {
    if (!hashProvider)
        return eve::Result<eve::ContentId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "settlement rule digest requires a hash provider",
            "hashProvider"));
    auto canonical = canonicalJson();
    if (!canonical) return eve::Result<eve::ContentId>::failure(canonical.status());
    return hashProvider(canonical.value());
}

}  // namespace eve::settlement
