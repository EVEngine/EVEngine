#pragma once
#include "common/Export.h"


/**
 * @file SkillCondition.h
 * @brief RPG Skill adapter for the renderer-independent decision protocol.
 */

#include "condition/Condition.h"

#include <functional>
#include <optional>
#include <string_view>

namespace eve::rpg {

class RPGActor;
struct SkillDefinition;

/**
 * @brief Read-only extension points for authority and policy queries.
 *
 * Callbacks run synchronously on the caller's thread during evaluation. They
 * must not mutate the actor, retain arguments, or call unknown code while
 * holding a lock. An empty callback means that capability is unavailable.
 */
struct SkillConditionQueries {
    using Authority = std::function<std::optional<bool>(std::string_view scope)>;
    using Policy =
        std::function<std::optional<condition::ConditionResult>(std::string_view name, const eve::Value& arguments)>;

    Authority authority;
    Policy    policy;
};

/**
 * @brief EvaluationContext backed by one borrowed RPG actor and SkillDefinition.
 *
 * The actor and definition are observed only for the synchronous call to
 * `Condition::evaluate`; this context does not own or retain them beyond that
 * call. Attributes and resource amounts are read from the canonical
 * AttributeSet. Tags include both skill-definition tags and active RPG status
 * tags.
 */
class EVENGINE_API_PLATFORM SkillConditionContext final : public condition::EvaluationContext {
public:
    /**
     * @brief Bind a read-only actor/skill pair for one synchronous evaluation.
     * @param actor Borrowed actor; it must outlive the evaluation call.
     * @param definition Borrowed skill definition; it must outlive the call.
     * @param queries Optional read-only authority and policy providers.
     */
    SkillConditionContext(const RPGActor* actor, const SkillDefinition& definition, SkillConditionQueries queries = {});

    /** @copydoc condition::EvaluationContext::value */
    /** @brief Value. */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override;
    /** @copydoc condition::EvaluationContext::policy */
    /** @brief Policy. */
    [[nodiscard]] std::optional<condition::ConditionResult> policy(std::string_view  name,
                                                                   const eve::Value& arguments) const override;

private:
    const RPGActor*        actor_      = nullptr;
    const SkillDefinition* definition_ = nullptr;
    SkillConditionQueries  queries_;
};

/** @brief Evaluates a condition using RPG Skill state without mutating it. */
class EVENGINE_API_PLATFORM SkillConditionAdapter {
public:
    /**
     * @brief Evaluate one skill condition against an actor and definition.
     * @param actor Borrowed actor observed synchronously; may be null.
     * @param definition Borrowed skill definition observed synchronously.
     * @param condition Owning, side-effect-free condition tree.
     * @param queries Optional read-only authority and policy providers.
     * @return A stable, UI-ready condition explanation.
     */
    [[nodiscard]] static condition::ConditionResult evaluate(const RPGActor* actor, const SkillDefinition& definition,
                                                             const condition::Condition& condition,
                                                             SkillConditionQueries       queries = {});
};

}  // namespace eve::rpg
