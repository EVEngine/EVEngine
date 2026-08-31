#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/LevelSystem.h"
#include "rpg/VitalsSystem.h"
#include "rpg/RPG.h"

#include <cmath>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.progression.levelAndXp") {
    RPGActor *actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 0.0);
    CHECK_EQ(actor->getXpToNext(), 100.0);

    actor->setXpToNext(100.0);
    actor->gainXp(50.0);
    CHECK_EQ(actor->getLevel(), 1);
    CHECK(approxEq(actor->getXp(), 50.0));

    actor->gainXp(60.0);  // 110 -> 升 1 级，剩 10
    CHECK_EQ(actor->getLevel(), 2);
    CHECK(approxEq(actor->getXp(), 10.0));
    CHECK(approxEq(actor->getXpToNext(), 120.0));  // 100 * 1.2

    actor->release();
}

TEST_CASE("rpg.progression.levelUpEventsViaFacade") {
    auto *rpg = RPG::create();
    rpg->update(0.f);  // 清掉其它用例遗留的静态事件
    RPGActor *actor = rpg->newActor();
    REQUIRE(actor != nullptr);
    actor->setXpToNext(50.0);
    CHECK(!actor->gainXp(20.0));
    CHECK(actor->gainXp(50.0));  // 升到 2

    rpg->update(0.f);
    CHECK(rpg->getLevelUpEventCount() == 1);
    CHECK(rpg->getLevelUpEventActor(0) == actor);
    CHECK_EQ(rpg->getLevelUpEventPreviousLevel(0), 1);
    CHECK_EQ(rpg->getLevelUpEventNewLevel(0), 2);

    actor->release();
}

TEST_CASE("rpg.progression.multiLevelUpSingleGain") {
    RPGActor *actor = RPGActor::createActor();
    actor->setXpToNext(10.0);
    CHECK(actor->gainXp(100.0));  // 连升多级
    CHECK(actor->getLevel() > 1);
    // 不变式：升级后经验必低于当前阈值
    CHECK(actor->getXp() < actor->getXpToNext());
    actor->release();
}

TEST_CASE("rpg.vitals.damageHealDeathRevive") {
    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    CHECK(approxEq(actor->getMax("hp"), 100.0));

    actor->setCurrent("hp", 80.0);
    CHECK(approxEq(actor->getCurrent("hp"), 80.0));
    CHECK(!actor->isDead("hp"));

    // 超出上限的 set 会被夹到 max
    actor->setCurrent("hp", 999.0);
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));

    // 伤害夹到 0，超量不把当前值打穿负数
    actor->setCurrent("hp", 50.0);
    double applied = actor->takeDamage("hp", 30.0, "slime");
    CHECK(approxEq(applied, 30.0));
    CHECK(approxEq(actor->getCurrent("hp"), 20.0));
    applied = actor->takeDamage("hp", 999.0, "slime");
    CHECK(approxEq(applied, 20.0));
    CHECK(approxEq(actor->getCurrent("hp"), 0.0));
    CHECK(actor->isDead("hp"));

    // 死亡后再受伤不生效
    applied = actor->takeDamage("hp", 10.0, "slime");
    CHECK(approxEq(applied, 0.0));

    // 复活到上限
    actor->revive("hp");
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));
    CHECK(!actor->isDead("hp"));

    // 治疗夹到 max
    actor->setCurrent("hp", 30.0);
    double healed = actor->heal("hp", 50.0);
    CHECK(approxEq(healed, 50.0));
    healed = actor->heal("hp", 999.0);
    CHECK(approxEq(healed, 20.0));
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));

    actor->release();
}

TEST_CASE("rpg.vitals.eventsViaFacade") {
    auto *rpg = RPG::create();
    rpg->update(0.f);  // 清掉其它用例遗留的静态事件
    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 100.0);
    actor->takeDamage("hp", 100.0, "boss");  // 归零死亡
    rpg->update(0.f);

    CHECK_EQ(rpg->getVitalsEventCount(), 2);  // damage + death
    bool sawDamage = false, sawDeath = false;
    for (int i = 0; i < rpg->getVitalsEventCount(); ++i) {
        if (rpg->getVitalsEventAction(i) == "damage" && rpg->getVitalsEventResource(i) == "hp") {
            sawDamage = true;
            CHECK(rpg->getVitalsEventActor(i) == actor);
            CHECK(approxEq(rpg->getVitalsEventAmount(i), 100.0));
            CHECK_EQ(rpg->getVitalsEventSource(i), std::string("boss"));
        }
        if (rpg->getVitalsEventAction(i) == "death") sawDeath = true;
    }
    CHECK(sawDamage);
    CHECK(sawDeath);
    actor->release();
}