#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/Trait.h"
#include "rpg/TraitSystem.h"
#include "rpg/RPG.h"

#include <cmath>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.trait.registryJsonAndCRUD") {
    TraitRegistry::clear();
    int n = TraitRegistry::loadFromJson(R"([
      {"id":"fire_resist",
       "traits":[
         {"kind":"elementRate","target":"fire","value":0.5},
         {"kind":"elementRate","target":"ice","value":1.5}
       ],
       "tags":["defense"]},
      {"id":"mighty",
       "traits":[
         {"kind":"paramRate","target":"attack","value":1.5},
         {"kind":"exParam","target":"critRate","value":0.3}
       ]}
    ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(TraitRegistry::count(), 2);
    const TraitDefinition *fr = TraitRegistry::find("fire_resist");
    REQUIRE(fr != nullptr);
    CHECK(fr->hasTag("defense"));
    REQUIRE(fr->traits.size() == 2);
    CHECK_EQ(fr->traits[0].target, std::string("fire"));
    CHECK(approxEq(fr->traits[0].value, 0.5));
    TraitRegistry::clear();
    CHECK_EQ(TraitRegistry::count(), 0);
}

TEST_CASE("rpg.trait.applyQueriesAndModifiers") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered = rpg->registerTraitsFromJson(R"([
      {"id":"mighty",
       "traits":[
         {"kind":"paramRate","target":"attack","value":1.5},
         {"kind":"exParam","target":"critRate","value":0.3}
       ]},
      {"id":"fire_resist","traits":[{"kind":"elementRate","target":"fire","value":0.5}]},
      {"id":"stone_skin","traits":[{"kind":"stateResist","target":"poison"}]},
      {"id":"brutal",
       "traits":[{"kind":"attackTimes","value":1},{"kind":"attackElement","target":"fire"}]}
    ])");
    CHECK_EQ(registered, 4);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100.0);

    int id = actor->applyTrait("mighty", "class");
    CHECK(id > 0);
    // paramRate 1.5 → mulAdd +0.5 → attack = 150
    CHECK(approxEq(actor->getFinalAttribute("attack"), 150.0));
    CHECK(approxEq(actor->getParamRate("attack"), 1.5));
    CHECK(approxEq(actor->getExParam("critRate"), 0.3));
    CHECK(actor->hasTrait("mighty"));
    CHECK_EQ(actor->getTraitCount(), 1);

    actor->applyTrait("fire_resist", "equipment");
    CHECK(approxEq(actor->getElementRate("fire"), 0.5));
    CHECK(approxEq(actor->getElementRate("ice"), 1.0));  // 未命中 → 1
    CHECK_EQ(actor->getTraitCount(), 2);

    actor->applyTrait("stone_skin", "race");
    CHECK(actor->isStateResist("poison"));
    CHECK(!actor->isStateResist("sleep"));

    actor->applyTrait("brutal", "passive");
    CHECK_EQ(actor->getAttackTimesAdd(), 1);
    const auto elems = actor->getAttackElements();
    REQUIRE(elems.size() == 1);
    CHECK_EQ(elems[0], std::string("fire"));

    // 移除按 source
    int removedEquip = actor->removeTraitsBySource("equipment");
    CHECK_EQ(removedEquip, 1);
    CHECK(approxEq(actor->getElementRate("fire"), 1.0));
    // 移除 paramRate 特征后属性加成随之撤销
    int removedMighty = actor->removeTraitsByTrait("mighty");
    CHECK_EQ(removedMighty, 1);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 100.0));
    CHECK(!actor->hasTrait("mighty"));

    rpg->clearTraitDefinitions();
    actor->release();
}

TEST_CASE("rpg.trait.facadeEventsIndependent") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered = rpg->registerTraitsFromJson(
        R"([{"id":"t1","traits":[{"kind":"exParam","target":"evasion","value":0.2}]}])");
    CHECK_EQ(registered, 1);
    RPGActor *a = rpg->newActor();
    int id = a->applyTrait("t1", "src");
    CHECK(id > 0);
    CHECK(approxEq(a->getExParam("evasion"), 0.2));
    // 两 actor 特征互不影响
    RPGActor *b = rpg->newActor();
    CHECK(approxEq(b->getExParam("evasion"), 0.0));
    rpg->clearTraitDefinitions();
    a->release();
    b->release();
}