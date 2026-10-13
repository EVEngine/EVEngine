#include "zeroerr/unittest.h"

#include "common/Value.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"

#include <utility>

TEST_CASE("rpg.state.canonicalRestoreRejectsLaterMalformedActorWithoutMutation") {
    eve::rpg::SkillRegistry::clear();
    auto* first  = eve::rpg::RPGActor::createActor();
    auto* second = eve::rpg::RPGActor::createActor();

    eve::rpg::SkillDefinition definition;
    definition.id = "fireball";
    eve::rpg::SkillRegistry::registerSkill(definition);
    first->learnSkill("fireball");
    first->setSkillCooldown("fireball", 3.f);

    auto captured = eve::rpg::captureRpgState();
    REQUIRE(captured.ok());
    auto state = std::move(captured).takeValue();
    state.at(0).find("skills")->set("fireball", eve::Value::number(9.0));
    state.at(1) = eve::Value::integer(42);

    auto restored = eve::rpg::restoreRpgState(state);
    CHECK(!restored.ok());
    CHECK(eve::rpg::SkillSystem::getCooldownRemaining(first, "fireball") == 3.f);

    second->release();
    first->release();
    eve::rpg::SkillRegistry::clear();
}
