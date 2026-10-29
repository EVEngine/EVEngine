// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "module-composition",
  "module": "rpg_balance"
}
*/
// clang-format on

#include "rpg/AttributeSystem.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/Class.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEvents.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/VitalsSystem.h"
#include "rpg/balance/RPGBalance.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <stdexcept>

namespace {
using namespace eve;
using namespace eve::rpg;
using eve::rpg::balance::RPGBalanceSuite;
Value fixture() {
    ClassDefinition cls;
    cls.id = "balance.test";
    ClassRegistry::registerClass(cls);
    SkillDefinition strike;
    strike.id         = "balance.test.strike";
    strike.cooldown   = 2;
    strike.targetType = "enemySingle";
    strike.tags       = {"attack"};
    auto cost         = resource::ResourceCost::create("mana", 5);
    if (!cost) throw std::runtime_error(cost.status().describe());
    auto spec = resource::CostSpec::create({std::move(cost).takeValue()});
    if (!spec) throw std::runtime_error(spec.status().describe());
    strike.cost = std::move(spec).takeValue();
    SkillRegistry::registerSkill(strike);
    SkillDamageSpec damage{"hp", "a.atk * a.power - b.def", "", 0.1, 90};
    auto            published = BattleSystem::registerSkillDamageChecked(strike.id, damage);
    if (!published) throw std::runtime_error(published.status().describe());
    auto parsed = Value::fromJson(R"({
        "schema":"evengine.rpg.balance.suite","version":1,
        "builds":[
            {"id":"a","classId":"balance.test","attributes":{"hp":100,"mana":20,"atk":10,"def":2,"speed":10,"power":2},
                "skills":["balance.test.strike"],"equipment":[{"slot":"weapon","stats":[{"attribute":"atk","op":"add","value":8}]}]},
            {"id":"b","classId":"balance.test","attributes":{"hp":100,"mana":20,"atk":10,"def":2,"speed":10,"power":2},
                "skills":["balance.test.strike"],"equipment":[{"slot":"weapon","stats":[{"attribute":"atk","op":"add","value":4}]}]}
        ],
        "strategies":[{"id":"burst","priority":["attack"],"reserveMana":0},{"id":"reserve","priority":["attack"],"reserveMana":0.8}],
        "scenarios":[{"id":"short","healthScale":1,"manaScale":1,"manaRegen":0},{"id":"long","healthScale":2,"manaScale":1,"manaRegen":3}],
        "bindings":[{"parameter":"power","build":"a","attribute":"power"}],
        "matchups":[{"left":"a","right":"b","min":0.6,"max":0.9}]
    })");
    if (!parsed) throw std::runtime_error(parsed.status().describe());
    return std::move(parsed).takeValue();
}
const Value& member(const Value& value, const char* name) { return value.getIf<Value::Object>()->at(name); }
double       numeric(const Value& value) {
    if (const auto* n = value.getIf<double>()) return *n;
    return double(*value.getIf<std::int64_t>());
}
}  // namespace

TEST_CASE("rpg.balance.productionCostsCooldownAndEquipmentProjection") {
    auto                                   config = fixture();
    ecs::Table                             world;
    ecs::ScopedTable                       table(world);
    eve::rpg::detail::SimulationEvents     events;
    eve::rpg::detail::SimulationEventScope scope(events);
    auto*                                  a = RPGActor::createActor();
    auto*                                  b = RPGActor::createActor();
    for (auto* actor : {a, b}) {
        actor->setBaseAttribute("hp", 200);
        actor->setBaseAttribute("mana", 20);
        actor->setBaseAttribute("atk", 10);
        actor->setBaseAttribute("power", 2);
        actor->learnSkill("balance.test.strike");
        VitalsSystem::revive(actor, "hp");
    }
    std::vector<EquipmentStat> gear{{"atk", "add", 8, 0}};
    REQUIRE(EquipmentSystem::projectSlot(*a, "weapon", gear).ok());
    REQUIRE_EQ(a->getFinalAttribute("atk"), 18.0);
    auto invalid  = gear;
    invalid[0].op = "unknown";
    REQUIRE(!EquipmentSystem::projectSlot(*a, "weapon", invalid).ok());
    REQUIRE_EQ(a->getFinalAttribute("atk"), 18.0);
    REQUIRE(EquipmentSystem::projectSlot(*a, "weapon", gear).ok());
    REQUIRE_EQ(a->getFinalAttribute("atk"), 18.0);
    Battle battle;
    battle.addActor(a, 0);
    battle.addActor(b, 1);
    REQUIRE(battle.configureSeed(10).ok());
    REQUIRE(battle.setActionChecked(a, "balance.test.strike", b).ok());
    battle.startRound();
    while (battle.executeNextAction()) {
    }
    REQUIRE_EQ(a->getBaseAttribute("mana"), 15.0);
    REQUIRE_EQ(a->getSkillCooldown("balance.test.strike"), 2.0f);
    const auto health = VitalsSystem::getCurrent(b, "hp");
    REQUIRE(battle.setActionChecked(a, "balance.test.strike", b).ok());
    battle.startRound();
    while (battle.executeNextAction()) {
    }
    REQUIRE_EQ(a->getBaseAttribute("mana"), 15.0);
    REQUIRE_EQ(VitalsSystem::getCurrent(b, "hp"), health);
    REQUIRE(!battle.configureSeed(1).ok());
}

TEST_CASE("rpg.balance.matrixIndependentStrategiesAndDeterministicEvidence") {
    RPGBalanceSuite suite;
    REQUIRE(suite.configure(fixture()).ok());
    auto targets = suite.targets();
    REQUIRE(targets.ok());
    REQUIRE_EQ(targets.value().getIf<Value::Array>()->size(), 1u);
    auto parameters = Value::object({{"power", 2.0}});
    auto seeds      = Value::array({Value(11), Value(29)});
    auto report     = suite.measure(parameters, seeds, 0.5, 16);
    REQUIRE(report.ok());
    REQUIRE_EQ(numeric(member(report.value(), "matches")), 32.0);
    REQUIRE_EQ(member(report.value(), "cells").getIf<Value::Array>()->size(), 8u);
    const auto& matrix = *member(report.value(), "matrix").getIf<Value::Array>();
    REQUIRE_EQ(numeric((*matrix[0].getIf<Value::Array>())[1]) + numeric((*matrix[1].getIf<Value::Array>())[0]), 1.0);
    bool   crossPolicy = false;
    double mana        = 0;
    for (const auto& cell : *member(report.value(), "cells").getIf<Value::Array>()) {
        if (member(cell, "leftStrategy") != member(cell, "rightStrategy")) crossPolicy = true;
        mana += numeric(member(cell, "meanManaSpent"));
    }
    REQUIRE(crossPolicy);
    REQUIRE(mana > 0);
    auto again = suite.measure(parameters, seeds, 0.5, 16);
    REQUIRE(again.ok());
    auto first  = report.value().toJson();
    auto second = again.value().toJson();
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value(), second.value());
}

TEST_CASE("rpg.balance.isolationFailureRecoveryAndTargetValidation") {
    auto            config = fixture();
    RPGBalanceSuite suite;
    REQUIRE(suite.configure(config).ok());
    ecs::Table                             liveWorld;
    ecs::ScopedTable                       table(liveWorld);
    eve::rpg::detail::SimulationEvents     liveEvents;
    eve::rpg::detail::SimulationEventScope scope(liveEvents);
    auto*                                  live = RPGActor::createActor();
    live->setBaseAttribute("hp", 100);
    live->setBaseAttribute("mana", 77);
    VitalsSystem::revive(live, "hp");
    liveEvents.vitals.clear();
    VitalsSystem::takeDamage(live, "hp", 2, "live sentinel");
    auto invalid = config;
    invalid.getIf<Value::Object>()
        ->at("matchups")
        .getIf<Value::Array>()
        ->push_back(Value::object({{"left", "b"}, {"right", "a"}, {"min", 0.6}, {"max", 0.9}}));
    REQUIRE(!suite.configure(invalid).ok());
    auto seeds      = Value::array({Value(1)});
    auto parameters = Value::object({{"power", 2.0}});
    REQUIRE(!suite.measure(Value::object({{"wrong", 2.0}}), seeds, 0.5, 8).ok());
    SkillSystem::registerCastCondition("balance.inject", [](RPGActor*, const SkillDefinition&, std::string&) -> bool {
        throw std::runtime_error("injected domain callback failure");
    });
    auto failed = suite.measure(parameters, seeds, 0.5, 8);
    SkillSystem::unregisterCastCondition("balance.inject");
    REQUIRE(!failed.ok());
    REQUIRE(suite.measure(parameters, seeds, 0.5, 8).ok());
    REQUIRE_EQ(live->getBaseAttribute("mana"), 77.0);
    REQUIRE_EQ(VitalsSystem::getCurrent(live, "hp"), 98.0);
    REQUIRE_EQ(RPGActor::liveActors().size(), 1u);
    REQUIRE_EQ(liveEvents.vitals.size(), 1u);
    REQUIRE_EQ(liveEvents.vitals[0].source, std::string("live sentinel"));
}
