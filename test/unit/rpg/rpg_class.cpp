#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/Class.h"
#include "rpg/RPG.h"

#include <cmath>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.class.registryJson") {
    ClassRegistry::clear();
    int n = ClassRegistry::loadFromJson(R"([
      {"id":"warrior","displayName":"战士","tags":["physical"],
       "traits":["mighty"],
       "learnSkills":[{"skillId":"power_strike","level":2},{"skillId":"cleave","level":5}]},
      {"id":"mage","displayName":"法师","learnSkills":[{"skillId":"fireball","level":1}]}
    ])");
    CHECK_EQ(n, 2);
    const ClassDefinition *w = ClassRegistry::find("warrior");
    REQUIRE(w != nullptr);
    CHECK(w->hasTag("physical"));
    REQUIRE(w->learnSkills.size() == 2);
    CHECK_EQ(w->learnSkills[1].skillId, std::string("cleave"));
    CHECK_EQ(w->learnSkills[1].level, 5);
    ClassRegistry::clear();
    CHECK_EQ(ClassRegistry::count(), 0);
}

TEST_CASE("rpg.class.setAppliesTraitsAndLearnsSkills") {
    auto *rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
    rpg->registerSkillsFromJson(R"([
      {"id":"power_strike"},{"id":"cleave"},{"id":"fireball"}
    ])");
    rpg->registerTraitsFromJson(R"([{"id":"mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5}]}])");
    int classes = rpg->registerClassesFromJson(R"([
      {"id":"warrior","traits":["mighty"],
       "learnSkills":[{"skillId":"power_strike","level":2},{"skillId":"cleave","level":5}]},
      {"id":"mage","learnSkills":[{"skillId":"fireball","level":1}]}
    ])");
    CHECK_EQ(classes, 2);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100.0);
    CHECK(actor->setClass("warrior"));
    CHECK(actor->hasClass("warrior"));
    CHECK_EQ(actor->getClassId(), std::string("warrior"));
    // 职业特征 → attack * 1.5
    CHECK(approxEq(actor->getFinalAttribute("attack"), 150.0));
    // 等级 1：power_strike(2) 未解锁，cleave(5) 未解锁
    CHECK(!actor->knowsSkill("power_strike"));
    CHECK_EQ(actor->getClassLearnCount(), 2);

    // 升到 3 级后补学
    actor->setXpToNext(10.0);
    int learned = actor->gainXp(30.0);  // 升多级
    CHECK(learned);
    int n = actor->checkLevelSkills();
    CHECK(n >= 1);
    CHECK(actor->knowsSkill("power_strike"));  // 2 级已解锁
    CHECK(!actor->knowsSkill("cleave"));       // 5 级仍未解锁

    // 换职业撤销旧职业特征
    CHECK(actor->setClass("mage"));
    CHECK(approxEq(actor->getFinalAttribute("attack"), 100.0));  // 职业特征撤销

    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
    actor->release();
}

TEST_CASE("rpg.class.checkLevelSkillsIdempotent") {
    auto *rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    rpg->registerSkillsFromJson(R"([{"id":"strike"}])");
    rpg->registerClassesFromJson(R"([{"id":"g","learnSkills":[{"skillId":"strike","level":1}]}])");
    RPGActor *actor = rpg->newActor();
    actor->setClass("g");
    CHECK(actor->knowsSkill("strike"));  // 1 级即时学到
    int again = actor->checkLevelSkills();
    CHECK_EQ(again, 0);  // 幂等，不再重复学
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    actor->release();
}