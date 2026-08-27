#include "decision/Condition.h"

#include <cmath>
#include <initializer_list>
#include <utility>

namespace eve::decision {
namespace {

Value object(std::initializer_list<std::pair<std::string, Value>> fields) {
    Value::Object result;
    for (auto&& field : fields) result.emplace(field.first, field.second);
    return Value(std::move(result));
}

bool number(const Value& value, double& result) {
    if (const auto* integer = value.getIf<std::int64_t>()) {
        result = static_cast<double>(*integer);
        return true;
    }
    if (const auto* real = value.getIf<double>()) {
        result = *real;
        return std::isfinite(result);
    }
    return false;
}

bool compareValues(const Value& left, CompareOperator op, const Value& right) {
    double leftNumber = 0.0;
    double rightNumber = 0.0;
    bool   numeric = number(left, leftNumber) && number(right, rightNumber);
    if (numeric) {
        switch (op) {
        case CompareOperator::Equal: return leftNumber == rightNumber;
        case CompareOperator::NotEqual: return leftNumber != rightNumber;
        case CompareOperator::Less: return leftNumber < rightNumber;
        case CompareOperator::LessEqual: return leftNumber <= rightNumber;
        case CompareOperator::Greater: return leftNumber > rightNumber;
        case CompareOperator::GreaterEqual: return leftNumber >= rightNumber;
        }
    }
    if (op == CompareOperator::Equal) return left == right;
    if (op == CompareOperator::NotEqual) return !(left == right);
    if (left.isString() && right.isString()) {
        const auto& l = *left.getIf<std::string>();
        const auto& r = *right.getIf<std::string>();
        switch (op) {
        case CompareOperator::Less: return l < r;
        case CompareOperator::LessEqual: return l <= r;
        case CompareOperator::Greater: return l > r;
        case CompareOperator::GreaterEqual: return l >= r;
        default: break;
        }
    }
    return false;
}

Value childDetails(const std::vector<ConditionResult>& results) {
    Value::Array children;
    children.reserve(results.size());
    for (const auto& result : results) {
        children.emplace_back(object({
            {"passed", Value(result.passed())},
            {"reason", Value(conditionReasonCodeName(result.reasonCode()))},
            {"evidence", result.evidence()},
            {"details", result.details()},
        }));
    }
    return Value(std::move(children));
}

ConditionResult unavailable(ConditionReasonCode reason, std::string_view kind, std::string_view key) {
    return ConditionResult::failed(reason, {}, object({
        {"kind", Value(std::string(kind))},
        {"key", Value(std::string(key))},
    }));
}

}  // namespace

const char* conditionReasonCodeName(ConditionReasonCode code) noexcept {
    switch (code) {
    case ConditionReasonCode::Passed: return "passed";
    case ConditionReasonCode::ChildFailed: return "child_failed";
    case ConditionReasonCode::NoChildPassed: return "no_child_passed";
    case ConditionReasonCode::Negated: return "negated";
    case ConditionReasonCode::MissingValue: return "missing_value";
    case ConditionReasonCode::ValueMismatch: return "value_mismatch";
    case ConditionReasonCode::TagMissing: return "tag_missing";
    case ConditionReasonCode::TagUnavailable: return "tag_unavailable";
    case ConditionReasonCode::AttributeMissing: return "attribute_missing";
    case ConditionReasonCode::ResourceMissing: return "resource_missing";
    case ConditionReasonCode::StateMissing: return "state_missing";
    case ConditionReasonCode::StateMismatch: return "state_mismatch";
    case ConditionReasonCode::AuthorityDenied: return "authority_denied";
    case ConditionReasonCode::AuthorityUnavailable: return "authority_unavailable";
    case ConditionReasonCode::PolicyRejected: return "policy_rejected";
    case ConditionReasonCode::PolicyUnavailable: return "policy_unavailable";
    case ConditionReasonCode::InvalidCondition: return "invalid_condition";
    }
    return "unknown";
}

const char* conditionKindName(ConditionKind kind) noexcept {
    switch (kind) {
    case ConditionKind::All: return "all";
    case ConditionKind::Any: return "any";
    case ConditionKind::Not: return "not";
    case ConditionKind::Compare: return "compare";
    case ConditionKind::HasTag: return "has_tag";
    case ConditionKind::HasAttribute: return "has_attribute";
    case ConditionKind::HasResource: return "has_resource";
    case ConditionKind::StateEquals: return "state_equals";
    case ConditionKind::AuthorityCheck: return "authority_check";
    case ConditionKind::PolicyCall: return "policy_call";
    }
    return "unknown";
}

const char* compareOperatorName(CompareOperator op) noexcept {
    switch (op) {
    case CompareOperator::Equal: return "eq";
    case CompareOperator::NotEqual: return "ne";
    case CompareOperator::Less: return "lt";
    case CompareOperator::LessEqual: return "le";
    case CompareOperator::Greater: return "gt";
    case CompareOperator::GreaterEqual: return "ge";
    }
    return "unknown";
}

ConditionResult ConditionResult::success(Value evidence, Value details) {
    return ConditionResult(true, ConditionReasonCode::Passed, std::move(evidence), std::move(details));
}

ConditionResult ConditionResult::failed(ConditionReasonCode reason, Value evidence, Value details) {
    return ConditionResult(false, reason, std::move(evidence), std::move(details));
}

Condition Condition::all(std::vector<Condition> children) {
    Condition result(ConditionKind::All);
    result.children_ = std::move(children);
    return result;
}

Condition Condition::any(std::vector<Condition> children) {
    Condition result(ConditionKind::Any);
    result.children_ = std::move(children);
    return result;
}

Condition Condition::not_(Condition child) {
    Condition result(ConditionKind::Not);
    result.children_.push_back(std::move(child));
    return result;
}

Condition Condition::compare(std::string key, CompareOperator op, Value expected) {
    Condition result(ConditionKind::Compare);
    result.key_ = std::move(key);
    result.compare_ = op;
    result.expected_ = std::move(expected);
    return result;
}

Condition Condition::hasTag(std::string tag) {
    Condition result(ConditionKind::HasTag);
    result.key_ = std::move(tag);
    return result;
}

Condition Condition::hasAttribute(std::string attribute) {
    Condition result(ConditionKind::HasAttribute);
    result.key_ = std::move(attribute);
    return result;
}

Condition Condition::hasResource(std::string resource) {
    Condition result(ConditionKind::HasResource);
    result.key_ = std::move(resource);
    return result;
}

Condition Condition::stateEquals(std::string key, Value expected) {
    Condition result(ConditionKind::StateEquals);
    result.key_ = std::move(key);
    result.expected_ = std::move(expected);
    return result;
}

Condition Condition::authorityCheck(std::string scope) {
    Condition result(ConditionKind::AuthorityCheck);
    result.key_ = std::move(scope);
    return result;
}

Condition Condition::policyCall(std::string name, Value arguments,
                                std::optional<ScriptConditionDeclaration> scriptDeclaration) {
    Condition result(ConditionKind::PolicyCall);
    result.key_ = std::move(name);
    result.arguments_ = std::move(arguments);
    result.scriptDeclaration_ = std::move(scriptDeclaration);
    return result;
}

bool Condition::isValid() const noexcept {
    switch (kind_) {
    case ConditionKind::All:
    case ConditionKind::Any:
        for (const auto& child : children_)
            if (!child.isValid()) return false;
        return true;
    case ConditionKind::Not:
        return children_.size() == 1 && children_.front().isValid();
    case ConditionKind::Compare:
    case ConditionKind::StateEquals:
        return !key_.empty();
    case ConditionKind::HasTag:
    case ConditionKind::HasAttribute:
    case ConditionKind::HasResource:
    case ConditionKind::AuthorityCheck:
        return !key_.empty();
    case ConditionKind::PolicyCall:
        return !key_.empty() && (!scriptDeclaration_ ||
                                 (scriptDeclaration_->name.empty() || scriptDeclaration_->name == key_));
    }
    return false;
}

ConditionResult Condition::evaluate(const EvaluationContext& context) const {
    if (!isValid()) return ConditionResult::failed(ConditionReasonCode::InvalidCondition);

    switch (kind_) {
    case ConditionKind::All: {
        std::vector<ConditionResult> results;
        results.reserve(children_.size());
        for (const auto& child : children_) {
            auto result = child.evaluate(context);
            if (!result.passed()) {
                const auto reason = result.reasonCode();
                results.push_back(std::move(result));
                return ConditionResult::failed(reason == ConditionReasonCode::InvalidCondition
                                                   ? reason
                                                   : ConditionReasonCode::ChildFailed,
                                               results.back().evidence(),
                                               object({{"kind", Value("all")}, {"children", childDetails(results)}}));
            }
            results.push_back(std::move(result));
        }
        return ConditionResult::success({}, object({{"kind", Value("all")}, {"children", childDetails(results)}}));
    }
    case ConditionKind::Any: {
        std::vector<ConditionResult> results;
        results.reserve(children_.size());
        for (const auto& child : children_) {
            auto result = child.evaluate(context);
            if (result.passed())
                return ConditionResult::success(result.evidence(),
                                               object({{"kind", Value("any")}, {"children", childDetails(results)}}));
            results.push_back(std::move(result));
        }
        return ConditionResult::failed(ConditionReasonCode::NoChildPassed, {},
                                       object({{"kind", Value("any")}, {"children", childDetails(results)}}));
    }
    case ConditionKind::Not: {
        auto result = children_.front().evaluate(context);
        if (result.passed())
            return ConditionResult::failed(ConditionReasonCode::Negated, result.evidence(),
                                           object({{"kind", Value("not")}, {"child", result.details()}}));
        return ConditionResult::success(result.evidence(),
                                       object({{"kind", Value("not")}, {"child", result.details()}}));
    }
    case ConditionKind::Compare: {
        auto actual = context.value(key_);
        if (!actual) return unavailable(ConditionReasonCode::MissingValue, "compare", key_);
        const bool equal = compareValues(*actual, compare_, expected_);
        return equal
                   ? ConditionResult::success(*actual, object({{"key", Value(key_)}, {"operator", Value(compareOperatorName(compare_))}, {"expected", expected_}}))
                   : ConditionResult::failed(ConditionReasonCode::ValueMismatch, *actual,
                                              object({{"key", Value(key_)}, {"operator", Value(compareOperatorName(compare_))}, {"expected", expected_}}));
    }
    case ConditionKind::HasTag: {
        auto actual = context.hasTag(key_);
        if (!actual) return unavailable(ConditionReasonCode::TagUnavailable, "has_tag", key_);
        return *actual ? ConditionResult::success(Value(true), object({{"tag", Value(key_)}}))
                       : ConditionResult::failed(ConditionReasonCode::TagMissing, Value(false), object({{"tag", Value(key_)}}));
    }
    case ConditionKind::HasAttribute: {
        auto actual = context.attribute(key_);
        if (!actual) return unavailable(ConditionReasonCode::AttributeMissing, "has_attribute", key_);
        return ConditionResult::success(*actual, object({{"attribute", Value(key_)}}));
    }
    case ConditionKind::HasResource: {
        auto actual = context.resource(key_);
        if (!actual) return unavailable(ConditionReasonCode::ResourceMissing, "has_resource", key_);
        return ConditionResult::success(*actual, object({{"resource", Value(key_)}}));
    }
    case ConditionKind::StateEquals: {
        auto actual = context.state(key_);
        if (!actual) return unavailable(ConditionReasonCode::StateMissing, "state_equals", key_);
        return compareValues(*actual, CompareOperator::Equal, expected_)
                   ? ConditionResult::success(*actual, object({{"state", Value(key_)}, {"expected", expected_}}))
                   : ConditionResult::failed(ConditionReasonCode::StateMismatch, *actual,
                                              object({{"state", Value(key_)}, {"expected", expected_}}));
    }
    case ConditionKind::AuthorityCheck: {
        auto allowed = context.authority(key_);
        if (!allowed) return unavailable(ConditionReasonCode::AuthorityUnavailable, "authority_check", key_);
        return *allowed ? ConditionResult::success(Value(true), object({{"scope", Value(key_)}}))
                        : ConditionResult::failed(ConditionReasonCode::AuthorityDenied, Value(false), object({{"scope", Value(key_)}}));
    }
    case ConditionKind::PolicyCall: {
        auto result = context.policy(key_, arguments_);
        if (!result) return unavailable(ConditionReasonCode::PolicyUnavailable, "policy_call", key_);
        if (result->passed()) return std::move(*result);
        return ConditionResult::failed(result->reasonCode(), result->evidence(),
                                       object({{"policy", Value(key_)}, {"details", result->details()}}));
    }
    }
    return ConditionResult::failed(ConditionReasonCode::InvalidCondition);
}

}  // namespace eve::decision
