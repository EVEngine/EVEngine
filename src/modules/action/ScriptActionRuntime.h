#pragma once
#include "common/Export.h"

/** @file ScriptActionRuntime.h
 *  @brief Script-owned projection over ActionRuntime + AbilityRuntime.
 *
 *  Responsibilities:
 *  - Unique ownership of one ActionRuntime / AbilityRuntime pair for EveScript
 *  - Parse script scalars/strings and project domain Result values to eve::Value
 *  - Shared ability projection surface reused by Combat (compose, do not reimplement)
 *
 *  Not responsible for:
 *  - Ability step orchestration (AbilityRuntime::step)
 *  - Timeline → AbilityDefinition assembly (abilityDefinitionFromTimeline)
 *  - Phase ownership (ActionRuntime)
 *  - Notify Enter/Update/Exit playback (hosts compose ActionNotifyPlayback)
 */

#include "action/AbilityRuntime.h"
#include "action/Action.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <string>

namespace eve::action {

/**
 * @brief Owner-thread script façade. Domain rules live on ActionRuntime / AbilityRuntime / AbilityAsset.
 *
 * @ownership Unique; transferred into a Squirrel instance by newRuntime().
 * @thread Owner/main thread only; not reentrant from callbacks.
 */
class EVENGINE_API_PLATFORM ScriptActionRuntime final {
public:
    ScriptActionRuntime()  = default;
    ~ScriptActionRuntime() = default;

    ScriptActionRuntime(const ScriptActionRuntime&)            = delete;
    ScriptActionRuntime& operator=(const ScriptActionRuntime&) = delete;
    ScriptActionRuntime(ScriptActionRuntime&&)                 = delete;
    ScriptActionRuntime& operator=(ScriptActionRuntime&&)      = delete;

    [[nodiscard]] Result<Value> registerAbilityJson(const std::string& abilityJson);
    [[nodiscard]] Result<Value> replaceAbilityJson(const std::string& abilityJson);
    /**
     * @brief Register one owning AbilityDefinition built by C++ (e.g. standard combat catalogue).
     * @remarks Script hosts normally use registerAbilityJson / registerTimelineAbility instead.
     */
    [[nodiscard]] Result<void> registerAbilityDefinition(AbilityDefinition definition);
    /** @brief Replace one registered definition for future activations. */
    [[nodiscard]] Result<void> replaceAbilityDefinition(AbilityDefinition definition);
    [[nodiscard]] Result<Value> registerTimelineAbility(const std::string& definitionText,
                                                        const std::string& timelineJson, float cooldownSeconds,
                                                        const std::string& instancingText, const std::string& groupText,
                                                        const std::string& triggerTag);
    [[nodiscard]] Result<Value> replaceTimelineAbility(const std::string& definitionText,
                                                       const std::string& timelineJson, float cooldownSeconds,
                                                       const std::string& instancingText, const std::string& groupText,
                                                       const std::string& triggerTag);
    [[nodiscard]] Result<Value> grantAbility(const std::string& ownerId, const std::string& definitionText);
    [[nodiscard]] Result<Value> revokeAbility(std::int64_t grantValue);
    [[nodiscard]] Result<Value> activateAbility(std::int64_t grantValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> advanceAbilities(std::int64_t tickValue, float seconds);
    [[nodiscard]] Result<Value> cancelAbility(std::int64_t activationValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> abilityGrant(std::int64_t grantValue) const;
    [[nodiscard]] Result<Value> matchingAbilities(const std::string& ownerId, const std::string& eventTag) const;
    [[nodiscard]] Result<Value> submitAction(const std::string& actionIdText, std::int64_t windupNs,
                                             std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tickValue);
    [[nodiscard]] Result<Value> advanceAction(std::int64_t executionValue, std::int64_t tickValue, float seconds);
    [[nodiscard]] Result<Value> cancelAction(std::int64_t executionValue, std::int64_t tickValue);
    [[nodiscard]] Result<Value> findAction(std::int64_t executionValue) const;
    [[nodiscard]] std::int64_t  executionCount() const;

private:
    [[nodiscard]] Result<Value> setAbilityJson(const std::string& abilityJson, bool replace);
    [[nodiscard]] Result<Value> setTimelineAbility(const std::string& definitionText, const std::string& timelineJson,
                                                   float cooldownSeconds, const std::string& instancingText,
                                                   const std::string& groupText, const std::string& triggerTag,
                                                   bool replace);
    [[nodiscard]] static Result<AbilityGrantId> parseGrant(std::int64_t value);
    [[nodiscard]] Result<Value>                 abilityGrantValue(AbilityGrantId grantId) const;

    ActionRuntime  actions_;
    AbilityRuntime abilities_{actions_};
};

}  // namespace eve::action
