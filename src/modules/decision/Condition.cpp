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
    double leftNumber  = 0.0;
    double rightNumber = 0.0;
    bool   numeric     = number(left, leftNumber) && number(right, rightNumber);
    if (numeric) {
        switch (op) {
            case CompareOperator::Equal: return leftNumber == rightNumber;
            case CompareOperator::NotEqual: return leftNumber != rightNumber;
            case CompareOperator::Less: return leftNumber < rightNumber;
            case CompareOperator::LessEqual: return leftNumber <= rightNumber;
            case CompareOperator::Greater: return leftNumber > rightNumber;
            case CompareOperator::GreaterEqual: return leftNumber >= rightNumber;
            case CompareOperator::Exists: return true;
        }
    }
    if (op == CompareOperator::Equal) return left == right;
    if (op == CompareOperator::NotEqual) return !(left == right);
    if (op == CompareOperator::Exists) return true;
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
    return ConditionResult::failed(reason, {},
                                   object({
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
        case CompareOperator::Exists: return "exists";
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
    Condition condition(ConditionKind::All);
    condition.children_ = std::move(children);
    return condition;
}

Condition Condition::any(std::vector<Condition> children) {
    Condition condition(ConditionKind::Any);
    condition.children_ = std::move(children);
    return condition;
}

Condition Condition::not_(Condition child) {
    Condition condition(ConditionKind::Not);
    condition.children_.push_back(std::move(child));
    return condition;
}

Condition Condition::compare(std::string key, CompareOperator op, Value expected) {
    Condition condition(ConditionKind::Compare);
    condition.key_      = std::move(key);
    condition.compare_  = op;
    condition.expected_ = std::move(expected);
    return condition;
}

Condition Condition::policyCall(std::string name, Value arguments,
                                std::optional<ScriptConditionDeclaration> scriptDeclaration) {
    Condition condition(ConditionKind::PolicyCall);
    condition.key_               = std::move(name);
    condition.arguments_         = std::move(arguments);
    condition.scriptDeclaration_ = std::move(scriptDeclaration);
    return condition;
}

bool Condition::isValid() const noexcept {
    switch (kind_) {
        case ConditionKind::All:
        case ConditionKind::Any: return true;
        case ConditionKind::Not: return children_.size() == 1;
        case ConditionKind::Compare: return !key_.empty();
        case ConditionKind::PolicyCall:
            return !key_.empty() &&
                   (!scriptDeclaration_ || (scriptDeclaration_->name.empty() || scriptDeclaration_->name == key_));
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
                    return ConditionResult::failed(
                        reason == ConditionReasonCode::InvalidCondition ? reason : ConditionReasonCode::ChildFailed,
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
                    return ConditionResult::success(
                        result.evidence(), object({{"kind", Value("any")}, {"children", childDetails(results)}}));
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
            if (compare_ == CompareOperator::Exists) {
                return ConditionResult::success(
                    *actual, object({{"key", Value(key_)}, {"operator", Value(compareOperatorName(compare_))}}));
            }
            const bool equal = compareValues(*actual, compare_, expected_);
            return equal ? ConditionResult::success(*actual, object({{"key", Value(key_)},
                                                                     {"operator", Value(compareOperatorName(compare_))},
                                                                     {"expected", expected_}}))
                         : ConditionResult::failed(ConditionReasonCode::ValueMismatch, *actual,
                                                   object({{"key", Value(key_)},
                                                           {"operator", Value(compareOperatorName(compare_))},
                                                           {"expected", expected_}}));
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
