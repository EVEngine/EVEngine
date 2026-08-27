#include "dialogue/DialogueState.h"

#include "common/Capability.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

template <class T, class Query>
std::optional<T> queryProviders(const std::string& subject, eve::IStateQuery* explicitProvider, Query&& query) {
    if (explicitProvider) {
        if (auto result = query(explicitProvider, subject); result.has_value()) return result;
    }

    eve::IStateQuery* service = eve::cap::query<eve::IStateQuery>();
    if (service && service != explicitProvider) {
        if (auto result = query(service, subject); result.has_value()) return result;
    }

    std::optional<T> result;
    eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery* provider) {
        if (result.has_value() || provider == explicitProvider || provider == service) return;
        result = query(provider, subject);
    });
    return result;
}

eve::Result<eve::decision::Condition> conditionError(std::string message, std::string path = {}) {
    return eve::Result<eve::decision::Condition>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool readString(const eve::Value& object, std::string_view key, std::string& out) {
    const eve::Value* value = object.find(std::string(key));
    if (!value || !value->isString()) return false;
    out = value->asString();
    return true;
}

bool readOperator(std::string_view spelling, eve::decision::CompareOperator& out) {
    if (spelling == "eq" || spelling == "equal" || spelling == "==") {
        out = eve::decision::CompareOperator::Equal;
        return true;
    }
    if (spelling == "ne" || spelling == "notEqual" || spelling == "!=") {
        out = eve::decision::CompareOperator::NotEqual;
        return true;
    }
    if (spelling == "lt" || spelling == "<") {
        out = eve::decision::CompareOperator::Less;
        return true;
    }
    if (spelling == "le" || spelling == "<=") {
        out = eve::decision::CompareOperator::LessEqual;
        return true;
    }
    if (spelling == "gt" || spelling == ">") {
        out = eve::decision::CompareOperator::Greater;
        return true;
    }
    if (spelling == "ge" || spelling == ">=") {
        out = eve::decision::CompareOperator::GreaterEqual;
        return true;
    }
    return false;
}

eve::Result<eve::decision::Condition> compileCondition(const eve::Value& specification) {
    if (specification.isArray()) {
        std::vector<eve::decision::Condition> children;
        children.reserve(specification.arraySize());
        for (std::size_t i = 0; i < specification.arraySize(); ++i) {
            auto child = compileCondition(specification.at(i));
            if (!child) return eve::Result<eve::decision::Condition>::failure(child.status());
            children.push_back(std::move(child).takeValue());
        }
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::all(std::move(children)));
    }
    if (!specification.isObject()) return conditionError("dialogue condition must be an object or array");

    if (const eve::Value* all = specification.find("all")) {
        if (!all->isArray()) return conditionError("condition 'all' must be an array", "all");
        std::vector<eve::decision::Condition> children;
        children.reserve(all->arraySize());
        for (std::size_t i = 0; i < all->arraySize(); ++i) {
            auto child = compileCondition(all->at(i));
            if (!child) return eve::Result<eve::decision::Condition>::failure(child.status());
            children.push_back(std::move(child).takeValue());
        }
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::all(std::move(children)));
    }
    if (const eve::Value* any = specification.find("any")) {
        if (!any->isArray()) return conditionError("condition 'any' must be an array", "any");
        std::vector<eve::decision::Condition> children;
        children.reserve(any->arraySize());
        for (std::size_t i = 0; i < any->arraySize(); ++i) {
            auto child = compileCondition(any->at(i));
            if (!child) return eve::Result<eve::decision::Condition>::failure(child.status());
            children.push_back(std::move(child).takeValue());
        }
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::any(std::move(children)));
    }
    if (const eve::Value* negated = specification.find("not")) {
        auto child = compileCondition(*negated);
        if (!child) return eve::Result<eve::decision::Condition>::failure(child.status());
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::not_(std::move(child).takeValue()));
    }

    const eve::Value* tag = specification.find("tag");
    if (!tag) tag = specification.find("hasTag");
    if (tag) {
        if (!tag->isString()) return conditionError("tag condition requires a string tag", "tag");
        return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::hasTag(tag->asString()));
    }

    const eve::Value* authority = specification.find("authority");
    if (authority) {
        if (!authority->isString()) return conditionError("authority condition requires a string scope", "authority");
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::authorityCheck(authority->asString()));
    }

    const eve::Value* policy = specification.find("policy");
    if (!policy) policy = specification.find("script");
    if (policy) {
        if (!policy->isString()) return conditionError("policy condition requires a string name", "policy");
        eve::Value arguments = eve::Value::Object{};
        if (const eve::Value* supplied = specification.find("arguments")) arguments = *supplied;
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::policyCall(policy->asString(), std::move(arguments)));
    }

    const eve::Value* state = specification.find("state");
    if (state && state->isString()) {
        const eve::Value* expected = specification.find("equals");
        if (!expected) expected = specification.find("value");
        if (!expected)
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::hasAttribute(state->asString()));
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::stateEquals(state->asString(), *expected));
    }

    const eve::Value* attribute = specification.find("attribute");
    const eve::Value* nameValue = specification.find("key");
    if (!nameValue) nameValue = specification.find("var");
    if (attribute && attribute->isString()) nameValue = attribute;

    const eve::Value* operation = specification.find("op");
    if (nameValue && operation) {
        if (!nameValue->isString() || !operation->isString())
            return conditionError("comparison condition requires string key and op");
        const std::string key = nameValue->asString();
        const std::string op  = operation->asString();
        if (op == "has")
            return eve::Result<eve::decision::Condition>::success(eve::decision::Condition::hasAttribute(key));
        if (op == "missing")
            return eve::Result<eve::decision::Condition>::success(
                eve::decision::Condition::not_(eve::decision::Condition::hasAttribute(key)));
        eve::decision::CompareOperator parsed;
        if (!readOperator(op, parsed)) return conditionError("unknown condition operator: " + op, "op");
        const eve::Value* expected = specification.find("value");
        if (!expected) return conditionError("comparison condition is missing value", "value");
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::compare(key, parsed, *expected));
    }

    if (attribute && attribute->isString())
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::hasAttribute(attribute->asString()));

    if (state && state->isObject()) {
        std::string key;
        if (!readString(*state, "key", key)) return conditionError("state condition is missing key", "state.key");
        const eve::Value* expected = state->find("equals");
        if (!expected) expected = state->find("value");
        if (!expected) return conditionError("state condition is missing value", "state.value");
        return eve::Result<eve::decision::Condition>::success(
            eve::decision::Condition::stateEquals(std::move(key), *expected));
    }

    if (const eve::Value* compare = specification.find("compare")) {
        if (!compare->isObject()) return conditionError("compare condition must be an object", "compare");
        return compileCondition(*compare);
    }

    return conditionError("unrecognized dialogue condition");
}

eve::Result<eve::MutationReceipt> mutationUnavailable() {
    return eve::Result<eve::MutationReceipt>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Unsupported, "dialogue world mutation requires an IStateMutation capability",
        "dialogue.state.mutation"));
}

}  // namespace

eve::Value toCanonicalValue(const eve::StateValue& value) {
    switch (value.kind()) {
        case eve::StateValue::Kind::Null: return eve::Value::null();
        case eve::StateValue::Kind::Int: return eve::Value::integer(value.asInt());
        case eve::StateValue::Kind::Float: return eve::Value::number(value.asDouble());
        case eve::StateValue::Kind::Bool: return eve::Value::boolean(value.asBool());
        case eve::StateValue::Kind::String: return eve::Value::string(value.asString());
        case eve::StateValue::Kind::Array: {
            eve::Value::Array result;
            result.reserve(value.arraySize());
            for (std::size_t i = 0; i < value.arraySize(); ++i) result.push_back(toCanonicalValue(value.at(i)));
            return eve::Value::array(std::move(result));
        }
        case eve::StateValue::Kind::Object: {
            eve::Value::Object result;
            for (const auto& key : value.keys()) result.emplace(key, toCanonicalValue(*value.find(key)));
            return eve::Value::object(std::move(result));
        }
    }
    return eve::Value::null();
}

eve::StateValue toDialogueStateValue(const eve::Value& value) {
    switch (value.type()) {
        case eve::Value::Type::Null: return eve::StateValue::null();
        case eve::Value::Type::Int64: return eve::StateValue::integer(value.asInt());
        case eve::Value::Type::Double: return eve::StateValue::number(value.asDouble());
        case eve::Value::Type::Bool: return eve::StateValue::boolean(value.asBool());
        case eve::Value::Type::String: return eve::StateValue::string(value.asString());
        case eve::Value::Type::Array: {
            eve::StateValue result = eve::StateValue::array();
            for (std::size_t i = 0; i < value.arraySize(); ++i) result.pushBack(toDialogueStateValue(value.at(i)));
            return result;
        }
        case eve::Value::Type::Object: {
            eve::StateValue result = eve::StateValue::object();
            for (const auto& key : value.keys()) result.set(key, toDialogueStateValue(*value.find(key)));
            return result;
        }
    }
    return eve::StateValue::null();
}

StateEvaluationContext::StateEvaluationContext(std::string subject, eve::IStateQuery* provider)
    : subject_(std::move(subject)), provider_(provider) {}

void StateEvaluationContext::setSubject(std::string subject) { subject_ = std::move(subject); }

std::optional<eve::Value> StateEvaluationContext::value(std::string_view key) const {
    return queryProviders<eve::Value>(subject_, provider_, [&](eve::IStateQuery* provider, const std::string& subject) {
        return provider->value(subject, key);
    });
}

std::optional<bool> StateEvaluationContext::hasTag(std::string_view tag) const {
    return queryProviders<bool>(subject_, provider_, [&](eve::IStateQuery* provider, const std::string& subject) {
        return provider->hasTag(subject, tag);
    });
}

std::optional<eve::Value> StateEvaluationContext::attribute(std::string_view key) const {
    auto result = queryProviders<eve::Value>(
        subject_, provider_,
        [&](eve::IStateQuery* provider, const std::string& subject) { return provider->attribute(subject, key); });
    if (result.has_value()) return result;
    return value(key);
}

std::optional<eve::Value> StateEvaluationContext::resource(std::string_view key) const {
    return queryProviders<eve::Value>(subject_, provider_, [&](eve::IStateQuery* provider, const std::string& subject) {
        return provider->resource(subject, key);
    });
}

std::optional<eve::Value> StateEvaluationContext::state(std::string_view key) const {
    auto result = queryProviders<eve::Value>(
        subject_, provider_,
        [&](eve::IStateQuery* provider, const std::string& subject) { return provider->state(subject, key); });
    if (result.has_value()) return result;
    return value(key);
}

std::optional<bool> StateEvaluationContext::authority(std::string_view scope) const {
    return queryProviders<bool>(subject_, provider_, [&](eve::IStateQuery* provider, const std::string& subject) {
        return provider->authority(subject, scope);
    });
}

std::optional<eve::decision::ConditionResult> StateEvaluationContext::policy(std::string_view  name,
                                                                             const eve::Value& arguments) const {
    if (!policyEvaluator_) return std::nullopt;
    return policyEvaluator_(name, arguments);
}

DialogueStateContext::DialogueStateContext(std::string subject) : subject_(std::move(subject)) {}

void DialogueStateContext::setSubject(std::string subject) { subject_ = std::move(subject); }

StateEvaluationContext DialogueStateContext::evaluationContext() const {
    StateEvaluationContext context(subject_, queryProvider_);
    context.setPolicyEvaluator(policyEvaluator_);
    return context;
}

std::optional<eve::Value> DialogueStateContext::value(std::string_view key) const {
    auto context = evaluationContext();
    return context.value(key);
}

std::optional<bool> DialogueStateContext::hasTag(std::string_view tag) const {
    auto context = evaluationContext();
    return context.hasTag(tag);
}

std::optional<eve::Value> DialogueStateContext::attribute(std::string_view key) const {
    auto context = evaluationContext();
    return context.attribute(key);
}

std::optional<eve::Value> DialogueStateContext::resource(std::string_view key) const {
    auto context = evaluationContext();
    return context.resource(key);
}

std::optional<eve::Value> DialogueStateContext::state(std::string_view key) const {
    auto context = evaluationContext();
    return context.state(key);
}

eve::Result<eve::decision::Condition> DialogueStateContext::compileCondition(const eve::Value& specification) const {
    return ::eve::dialogue::compileCondition(specification);
}

eve::decision::ConditionResult DialogueStateContext::evaluate(const eve::decision::Condition& condition) const {
    auto context = evaluationContext();
    return condition.evaluate(context);
}

eve::decision::ConditionResult DialogueStateContext::evaluate(const eve::Value& specification) const {
    auto compiled = compileCondition(specification);
    if (!compiled) {
        const eve::Diagnostic* diagnostic = compiled.error();
        const std::string      message    = diagnostic ? diagnostic->message() : "invalid dialogue condition";
        return eve::decision::ConditionResult::failed(eve::decision::ConditionReasonCode::InvalidCondition,
                                                      eve::Value(), eve::Value::Object{{"error", eve::Value(message)}});
    }
    eve::decision::Condition condition = std::move(compiled).takeValue();
    return evaluate(condition);
}

eve::Result<eve::MutationReceipt> DialogueStateContext::apply(std::span<const eve::StateMutation> mutations,
                                                              const eve::MutationContext&         context) const {
    eve::IStateMutation* provider = mutationProvider_;
    if (!provider) provider = eve::cap::query<eve::IStateMutation>();
    if (!provider) return mutationUnavailable();
    return provider->apply(mutations, context);
}

}  // namespace eve::dialogue
