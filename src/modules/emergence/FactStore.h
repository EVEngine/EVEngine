#pragma once
#include "common/Export.h"

/**
 * @file FactStore.h
 * @brief Authoritative, rule-visible world facts implementing EvaluationContext.
 */

#include "common/Result.h"
#include "common/Value.h"
#include "decision/Condition.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::emergence {

/** @brief Domain partition of a fact key used by the watch index. */
enum class FactDomain : std::uint8_t {
    Value,
    Tag,
    Attribute,
    Resource,
    State,
    Authority,
    Policy,
};

/** @brief Whether a FactStore mutation changed stored state. */
enum class FactChange : std::uint8_t { Unchanged = 0, Changed = 1 };

/**
 * @brief Build the canonical watch key for one fact domain.
 * @param domain Fact namespace.
 * @param name Non-empty leaf name.
 * @return Owning `domain:name` key, or empty when name is empty.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string makeFactKey(FactDomain domain, std::string_view name);

/**
 * @brief Parse a canonical watch key into domain and leaf name.
 * @return Structured failure when the key is empty or uses an unknown domain prefix.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<std::pair<FactDomain, std::string>> parseFactKey(
    std::string_view key);

/**
 * @brief Mutable fact bag that doubles as a read-only Condition evaluation context.
 *
 * FactStore is the sole owner of rule-visible values. Mutations report whether the
 * stored value changed so the RuleEngine can wake only interested watchers.
 *
 * @thread Owning simulation thread only; no synchronization is performed.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_FOUNDATION FactStore final : public decision::EvaluationContext {
public:
    /** @brief Clear every fact without invoking callbacks. */
    void clear();

    /**
     * @brief Set a compare/value fact.
     * @return FactChange::Changed when the stored value differed.
     */
    [[nodiscard]] FactChange setValue(std::string key, eve::Value value);
    /** @brief Set tag membership; present=true means the tag exists. */
    [[nodiscard]] FactChange setTag(std::string tag, bool present);
    /** @brief Set an attribute value. */
    [[nodiscard]] FactChange setAttribute(std::string key, eve::Value value);
    /** @brief Set a resource value. */
    [[nodiscard]] FactChange setResource(std::string key, eve::Value value);
    /** @brief Set a state value. */
    [[nodiscard]] FactChange setState(std::string key, eve::Value value);
    /** @brief Set authority for a scope. */
    [[nodiscard]] FactChange setAuthority(std::string scope, bool granted);
    /**
     * @brief Register or replace a read-only policy result.
     * @return FactChange::Changed when the stored result differed.
     */
    [[nodiscard]] FactChange setPolicy(std::string name, decision::ConditionResult result);

    /** @brief Erase a value fact. */
    [[nodiscard]] FactChange clearValue(std::string_view key);
    /** @brief Erase a tag fact. */
    [[nodiscard]] FactChange clearTag(std::string_view tag);

    [[nodiscard]] std::optional<eve::Value>                value(std::string_view key) const override;
    [[nodiscard]] std::optional<bool>                      hasTag(std::string_view tag) const override;
    [[nodiscard]] std::optional<eve::Value>                attribute(std::string_view key) const override;
    [[nodiscard]] std::optional<eve::Value>                resource(std::string_view key) const override;
    [[nodiscard]] std::optional<eve::Value>                state(std::string_view key) const override;
    [[nodiscard]] std::optional<bool>                      authority(std::string_view scope) const override;
    [[nodiscard]] std::optional<decision::ConditionResult> policy(std::string_view  name,
                                                                  const eve::Value& arguments) const override;

    /** @brief Export deterministic compact JSON for runtime snapshots. */
    [[nodiscard]] std::string snapshotJson() const;
    /**
     * @brief Transactionally restore facts from a snapshot produced by snapshotJson().
     * @return Success, or structured failure leaving the prior store unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreJson(std::string_view json);

    /** @brief Number of stored value facts. */
    [[nodiscard]] int valueCount() const noexcept { return static_cast<int>(values_.size()); }

private:
    template <class Map, class T>
    static FactChange assign(Map& map, std::string key, T value);

    std::unordered_map<std::string, eve::Value>                values_;
    std::unordered_map<std::string, bool>                      tags_;
    std::unordered_map<std::string, eve::Value>                attributes_;
    std::unordered_map<std::string, eve::Value>                resources_;
    std::unordered_map<std::string, eve::Value>                states_;
    std::unordered_map<std::string, bool>                      authorities_;
    std::unordered_map<std::string, decision::ConditionResult> policies_;
};

}  // namespace eve::emergence
