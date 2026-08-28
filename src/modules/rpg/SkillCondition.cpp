#include "rpg/SkillCondition.h"

#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

#include <string>

namespace eve::rpg {
namespace {

std::optional<eve::Value> skillValue(const RPGActor* actor, const SkillDefinition& definition, std::string_view key) {
    if (!actor) return std::nullopt;
    auto*       mutableActor = const_cast<RPGActor*>(actor);
    const auto* runtime      = [&]() -> const SkillRuntime* {
        const auto it = mutableActor->skills()->known.find(definition.id);
        return it == mutableActor->skills()->known.end() ? nullptr : &it->second;
    }();
    if (key == "skill.id") return eve::Value(definition.id);
    if (key == "skill.targetType") return eve::Value(definition.targetType);
    if (key == "skill.known") return eve::Value(runtime != nullptr);
    if (key == "skill.casting") return eve::Value(mutableActor->skills()->casting.active);
    if (key == "skill.cooldown") return eve::Value(runtime ? runtime->cooldownRemaining : 0.0);
    if (key == "skill.phase") return eve::Value(mutableActor->skills()->casting.active ? "casting" : "idle");
    return std::nullopt;
}

}  // namespace

SkillConditionContext::SkillConditionContext(const RPGActor* actor, const SkillDefinition& definition,
                                             SkillConditionQueries queries)
    : actor_(actor), definition_(&definition), queries_(std::move(queries)) {}

std::optional<eve::Value> SkillConditionContext::value(std::string_view key) const {
    return skillValue(actor_, *definition_, key);
}

std::optional<bool> SkillConditionContext::hasTag(std::string_view tag) const {
    if (!actor_) return std::nullopt;
    if (definition_->hasTag(std::string(tag))) return true;
    return const_cast<RPGActor*>(actor_)->hasStatusTag(std::string(tag));
}

std::optional<eve::Value> SkillConditionContext::attribute(std::string_view key) const {
    if (!actor_ || key.empty()) return std::nullopt;
    auto* mutableActor = const_cast<RPGActor*>(actor_);
    if (!AttributeSystem::hasAttribute(mutableActor, std::string(key))) return std::nullopt;
    return eve::Value(AttributeSystem::getFinal(mutableActor, std::string(key)));
}

std::optional<eve::Value> SkillConditionContext::resource(std::string_view key) const {
    // RPG resources are attributes by design (mana, stamina, and game-defined
    // resources use the same canonical AttributeSet source).
    return attribute(key);
}

std::optional<eve::Value> SkillConditionContext::state(std::string_view key) const { return value(key); }

std::optional<bool> SkillConditionContext::authority(std::string_view scope) const {
    if (!queries_.authority) return std::nullopt;
    return queries_.authority(scope);
}

std::optional<decision::ConditionResult> SkillConditionContext::policy(std::string_view  name,
                                                                       const eve::Value& arguments) const {
    if (!queries_.policy) return std::nullopt;
    return queries_.policy(name, arguments);
}

decision::ConditionResult SkillConditionAdapter::evaluate(const RPGActor* actor, const SkillDefinition& definition,
                                                          const decision::Condition& condition,
                                                          SkillConditionQueries      queries) {
    SkillConditionContext context(actor, definition, std::move(queries));
    return condition.evaluate(context);
}

}  // namespace eve::rpg
