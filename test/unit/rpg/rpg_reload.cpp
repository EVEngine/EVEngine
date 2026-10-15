#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/StateValue.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"

#include <string>

using namespace eve::rpg;

TEST_CASE("rpg.state.captureRestoreCastingAndCooldown") {
    SkillRegistry::clear();
    RPGActor* actor = RPGActor::createActor();

    SkillDefinition def;
    def.id       = "fireball";
    def.castTime = 1.5f;
    SkillRegistry::registerSkill(def);
    actor->learnSkill("fireball");
    actor->setSkillCooldown("fireball", 2.5f);
    CHECK(eve::rpg::SkillSystem::beginCast(actor, "fireball"));
    CHECK(eve::rpg::SkillSystem::isCasting(actor));

    eve::StateValue captured;
    REQUIRE(eve::rpg::captureRpgState(captured));

    // Mutate everything, then restore.
    eve::rpg::SkillSystem::cancelCast(actor);
    actor->setSkillCooldown("fireball", 0.f);
    CHECK(!eve::rpg::SkillSystem::isCasting(actor));

    std::string err;
    CHECK(eve::rpg::restoreRpgState(captured, &err));
    CHECK(err.empty());
    CHECK(eve::rpg::SkillSystem::isCasting(actor));
    CHECK_EQ(eve::rpg::SkillSystem::getCastingSkillId(actor), std::string("fireball"));
    CHECK(eve::rpg::SkillSystem::getCooldownRemaining(actor, "fireball") == 2.5f);

    actor->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.state.resetCancelsCasting") {
    SkillRegistry::clear();
    RPGActor* actor = RPGActor::createActor();

    SkillDefinition def;
    def.id       = "channel";
    def.castTime = 1.f;
    SkillRegistry::registerSkill(def);
    actor->learnSkill("channel");
    CHECK(eve::rpg::SkillSystem::beginCast(actor, "channel"));
    CHECK(eve::rpg::SkillSystem::isCasting(actor));

    CHECK(eve::rpg::resetRpgState());
    CHECK(!eve::rpg::SkillSystem::isCasting(actor));

    actor->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.state.restoreRejectsMalformed") {
    std::string err;
    CHECK(!eve::rpg::restoreRpgState(eve::StateValue::object(), &err));
    CHECK(!err.empty());
}
