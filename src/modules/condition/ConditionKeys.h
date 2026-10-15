#pragma once
#include "common/Export.h"

/**
 * @file ConditionKeys.h
 * @brief Canonical Compare key prefixes and PolicyCall names for thin conditions.
 *
 * Domain leaves (tag / attribute / resource / state / authority) are conventions
 * over Compare keys and PolicyCall names, not ConditionKind variants.
 */

#include "condition/Condition.h"

#include <optional>
#include <string>
#include <string_view>

namespace eve::condition {

/** @brief Prefix for tag membership booleans (`tag:<name>`). */
inline constexpr std::string_view kTagPrefix = "tag:";
/** @brief Prefix for attribute values (`attr:<name>`). */
inline constexpr std::string_view kAttrPrefix = "attr:";
/** @brief Prefix for resource values (`resource:<name>`). */
inline constexpr std::string_view kResourcePrefix = "resource:";
/** @brief Prefix for state values (`state:<name>`). */
inline constexpr std::string_view kStatePrefix = "state:";
/** @brief Prefix for authority scopes used as watch/dependency keys. */
inline constexpr std::string_view kAuthorityPrefix = "authority:";
/** @brief Prefix for policy watch keys (`policy:<name>`). */
inline constexpr std::string_view kPolicyPrefix = "policy:";
/** @brief PolicyCall name for read-only authority checks. */
inline constexpr std::string_view kAuthorityAllowPolicy = "authority.allow";

/**
 * @brief Build a `tag:<name>` Compare key.
 * @param tag Non-empty tag name without a domain prefix.
 */
[[nodiscard]] inline std::string tagKey(std::string_view tag) {
    std::string key(kTagPrefix);
    key.append(tag);
    return key;
}

/**
 * @brief Build an `attr:<name>` Compare key.
 * @param name Non-empty attribute name without a domain prefix.
 */
[[nodiscard]] inline std::string attrKey(std::string_view name) {
    std::string key(kAttrPrefix);
    key.append(name);
    return key;
}

/**
 * @brief Build a `resource:<name>` Compare key.
 * @param name Non-empty resource name without a domain prefix.
 */
[[nodiscard]] inline std::string resourceKey(std::string_view name) {
    std::string key(kResourcePrefix);
    key.append(name);
    return key;
}

/**
 * @brief Build a `state:<name>` Compare key.
 * @param name Non-empty state name without a domain prefix.
 */
[[nodiscard]] inline std::string stateKey(std::string_view name) {
    std::string key(kStatePrefix);
    key.append(name);
    return key;
}

/**
 * @brief Build an `authority:<scope>` dependency/watch key.
 * @param scope Non-empty authority scope without a domain prefix.
 */
[[nodiscard]] inline std::string authorityKey(std::string_view scope) {
    std::string key(kAuthorityPrefix);
    key.append(scope);
    return key;
}

/**
 * @brief Strip a known domain prefix from a key.
 * @return The remainder when `key` starts with `prefix`; otherwise nullopt.
 */
[[nodiscard]] inline std::optional<std::string_view> stripPrefix(std::string_view key, std::string_view prefix) {
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    return key.substr(prefix.size());
}

/**
 * @brief Compare that a tag key is present (`tag:<name> == true`).
 * @param tag Tag name without prefix.
 */
[[nodiscard]] inline Condition tagPresent(std::string tag) {
    return Condition::compare(tagKey(std::move(tag)), CompareOperator::Equal, Value(true));
}

/**
 * @brief Compare that an attribute key exists (`attr:<name>` Exists).
 * @param name Attribute name without prefix.
 */
[[nodiscard]] inline Condition attributeExists(std::string name) {
    return Condition::compare(attrKey(std::move(name)), CompareOperator::Exists, {});
}

/**
 * @brief Compare that a resource key exists (`resource:<name>` Exists).
 * @param name Resource name without prefix.
 */
[[nodiscard]] inline Condition resourceExists(std::string name) {
    return Condition::compare(resourceKey(std::move(name)), CompareOperator::Exists, {});
}

/**
 * @brief Compare that a state key equals an expected value.
 * @param name State name without prefix.
 * @param expected Expected scalar or structured value.
 */
[[nodiscard]] inline Condition stateEquals(std::string name, Value expected) {
    return Condition::compare(stateKey(std::move(name)), CompareOperator::Equal, std::move(expected));
}

/**
 * @brief PolicyCall for read-only authority (`authority.allow`).
 * @param scope Authority scope without prefix.
 */
[[nodiscard]] inline Condition authorityAllow(std::string scope) {
    ScriptConditionDeclaration declaration;
    declaration.name         = std::string(kAuthorityAllowPolicy);
    declaration.dependencies = {authorityKey(scope)};
    declaration.determinism  = DeterminismLevel::BitExact;
    return Condition::policyCall(std::string(kAuthorityAllowPolicy), Value::Object{{"scope", Value(scope)}},
                                 std::move(declaration));
}

/**
 * @brief Extract the `scope` string from an `authority.allow` argument object.
 * @param arguments PolicyCall arguments.
 * @return Non-empty scope text, or nullopt when the field is missing/invalid.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::optional<std::string> authorityAllowScope(const Value& arguments);

/**
 * @brief Resolve the built-in `authority.allow` policy when `name` matches.
 *
 * @param name Policy name from PolicyCall.
 * @param arguments Policy arguments; requires a non-empty string `scope`.
 * @param authorityQuery Returns known grant, or nullopt when the authority
 *        source is unavailable for that scope.
 * @return A ConditionResult when this is the authority policy; nullopt when
 *         `name` is a different policy the caller should handle.
 */
template <class AuthorityQuery>
[[nodiscard]] std::optional<ConditionResult> resolveAuthorityAllow(std::string_view name, const Value& arguments,
                                                                   AuthorityQuery&& authorityQuery) {
    if (name != kAuthorityAllowPolicy) return std::nullopt;
    auto scope = authorityAllowScope(arguments);
    if (!scope) {
        return ConditionResult::failed(ConditionReasonCode::InvalidCondition, {},
                                       Value::Object{{"policy", Value(std::string(kAuthorityAllowPolicy))},
                                                     {"error", Value("authority.allow requires string scope")}});
    }
    auto allowed = authorityQuery(std::string_view(*scope));
    if (!allowed) {
        return ConditionResult::failed(ConditionReasonCode::AuthorityUnavailable, {},
                                       Value::Object{{"scope", Value(*scope)}});
    }
    if (*allowed) {
        return ConditionResult::success(Value(true), Value::Object{{"scope", Value(*scope)}});
    }
    return ConditionResult::failed(ConditionReasonCode::AuthorityDenied, Value(false),
                                   Value::Object{{"scope", Value(*scope)}});
}

}  // namespace eve::condition
