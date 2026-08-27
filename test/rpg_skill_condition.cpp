#include "rpg/Skill.h"
#include "rpg/SkillCondition.h"
#include "rpg/SkillSystem.h"
#include "rpg/RPGActor.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <optional>

TEST_CASE("rpg.skillConditionAdapterReadsCanonicalSkillState") {
    using namespace eve::decision;
    using namespace eve::rpg;

    RPGActor* actor = RPGActor::createActor();
    actor->setBaseAttribute("mana", 20.0);
    actor->learnSkill("skill.fire");
    SkillDefinition definition;
    definition.id = "skill.fire";
    definition.tags = {"fire"};

    const auto condition = Condition::all({
        Condition::hasTag("fire"),
        Condition::hasAttribute("mana"),
        Condition::hasResource("mana"),
        Condition::compare("skill.cooldown", CompareOperator::Equal, 0.0),
        Condition::stateEquals("skill.phase", "idle"),
    });
    const auto result = SkillConditionAdapter::evaluate(actor, definition, condition);
    REQUIRE(result.passed());
    CHECK_EQ(static_cast<int>(result.reasonCode()), static_cast<int>(ConditionReasonCode::Passed));

    actor->setSkillCooldown("skill.fire", 2.0f);
    const auto blocked = SkillConditionAdapter::evaluate(
        actor, definition, Condition::compare("skill.cooldown", CompareOperator::Equal, 0.0));
    REQUIRE(!blocked.passed());
    CHECK_EQ(static_cast<int>(blocked.reasonCode()), static_cast<int>(ConditionReasonCode::ValueMismatch));
    actor->release();
}

TEST_CASE("rpg.skillSystemUsesConditionResultAsCompatibilityReason") {
    using namespace eve::decision;
    using namespace eve::rpg;

    SkillDefinition definition;
    definition.id = "skill.guard";
    definition.castCondition = Condition::hasTag("silenced");
    SkillRegistry::registerSkill(definition);
    RPGActor* actor = RPGActor::createActor();
    actor->learnSkill(definition.id);

    CHECK(!actor->canCastSkill(definition.id));
    CHECK_EQ(actor->canCastSkillReason(definition.id), "tag_missing");
    actor->release();
    SkillRegistry::remove(definition.id);
}

TEST_CASE("rpg.skillConditionPolicyAndAuthorityAreReadOnlyProviders") {
    using namespace eve::decision;
    using namespace eve::rpg;

    RPGActor* actor = RPGActor::createActor();
    SkillDefinition definition;
    definition.id = "skill.policy";
    int authorityCalls = 0;
    int policyCalls = 0;
    SkillConditionQueries queries;
    queries.authority = [&authorityCalls](std::string_view scope) -> std::optional<bool> {
        ++authorityCalls;
        return scope == "cast";
    };
    queries.policy = [&policyCalls](std::string_view name,
                                    const eve::Value&) -> std::optional<ConditionResult> {
        ++policyCalls;
        return name == "safe" ? std::optional<ConditionResult>(ConditionResult::success())
                               : std::nullopt;
    };
    const auto result = SkillConditionAdapter::evaluate(
        actor, definition,
        Condition::all({Condition::authorityCheck("cast"), Condition::policyCall("safe")}), queries);
    REQUIRE(result.passed());
    CHECK_EQ(authorityCalls, 1);
    CHECK_EQ(policyCalls, 1);
    actor->release();
}
