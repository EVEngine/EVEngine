#pragma once
#include "common/Export.h"

/** @file ScriptActionRuntime.h
 *  @brief Script-owned ActionRuntime + AbilityRuntime façade used by EveScript.
 *
 *  This type is the Squirrel `ActionRuntime` instance created by
 *  `eve.Action().newRuntime()`. C++ gameplay code should use
 *  `eve::action::ActionRuntime` / `AbilityRuntime` directly instead.
 */

#include "action/AbilitySystem.h"
#include "action/Action.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <map>
#include <string>

namespace eve::action {

/**
 * @brief Owner-thread script façade that uniquely owns one ActionRuntime and a
 *        borrowing AbilityRuntime.
 *
 * Phase, cooldown and grant identity remain owned by those C++ runtimes. Script
 * only injects tick/delta and observes Result projections as `eve::Value`.
 *
 * @ownership Unique; transferred into a Squirrel instance by newRuntime().
 * @thread Owner/main thread only; not reentrant from callbacks.
 */
class EVENGINE_API_PLATFORM ScriptActionRuntime final {
public:
    /** @brief Construct an empty script-owned action/ability runtime pair. */
    ScriptActionRuntime() = default;
    /** @brief Releases owned ActionRuntime / AbilityRuntime state. */
    ~ScriptActionRuntime() = default;

    ScriptActionRuntime(const ScriptActionRuntime&) = delete;
    ScriptActionRuntime& operator=(const ScriptActionRuntime&) = delete;
    ScriptActionRuntime(ScriptActionRuntime&&) = delete;
    ScriptActionRuntime& operator=(ScriptActionRuntime&&) = delete;

    [[nodiscard]] Result<Value> registerAbilityJson(const std::string& abilityJson);
    [[nodiscard]] Result<Value> replaceAbilityJson(const std::string& abilityJson);
    [[nodiscard]] Result<Value> registerTimelineAbility(const std::string& definitionText, const std::string& timelineJson, double cooldownSeconds, const std::string& instancingText, const std::string& groupText, const std::string& triggerTag);
    [[nodiscard]] Result<Value> replaceTimelineAbility(const std::string& definitionText, const std::string& timelineJson, double cooldownSeconds, const std::string& instancingText, const std::string& groupText, const std::string& triggerTag);
    [[nodiscard]] Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText);
    [[nodiscard]] Result<Value> revokeAbility(std::int64_t grantValue);
    [[nodiscard]] Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> advanceAbilities(std::int64_t tickValue, double seconds);
    [[nodiscard]] Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> abilityGrant(std::int64_t grantValue) const;
    [[nodiscard]] Result<Value> matchingAbilities(const std::string& ownerId, const std::string& eventTag) const;
    [[nodiscard]] Result<Value> submitAction(const std::string& actionIdText, std::int64_t windupNs, std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tickValue);
    [[nodiscard]] Result<Value> advanceAction(std::int64_t executionValue, std::int64_t tickValue, double seconds);
    [[nodiscard]] Result<Value> cancelAction(std::int64_t executionValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> findAction(std::int64_t executionValue) const;
    [[nodiscard]] std::int64_t executionCount() const;

private:
    [[nodiscard]] Result<Value> setTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                                   double cooldownSeconds, const std::string& instancingText,
                                                   const std::string& groupText, const std::string& triggerTag,
                                                   bool replace);
    [[nodiscard]] static Result<AbilityGrantId> parseGrant(std::int64_t value);
    [[nodiscard]] Result<Value> abilityGrantValue(AbilityGrantId grantId) const;
    [[nodiscard]] static Value abilityActivationValue(const AbilityActivation& activation);

    ActionRuntime                                 actions_;
    AbilityRuntime                                abilities_{actions_};
    std::map<std::string, LogicalId, std::less<>> abilityActions_;
};

}  // namespace eve::action
