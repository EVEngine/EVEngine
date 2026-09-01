#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/LootSystem.h"
#include "rpg/RPG.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"

#include <cmath>
#include <random>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.equipment.statSyncAppliesAndRemoves") {
    auto *rpg = RPG::create();
    rpg->clearAllItemStats();
    int registered = rpg->registerItemStatsFromJson(
        "sword",
        R"([
           {"attribute":"attack","op":"add","value":15},
           {"attribute":"attack","op":"mulMul","value":0.1}
         ])");
    CHECK_EQ(registered, 2);
    CHECK_EQ(rpg->getItemStatCount(), 1);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 10.0);

    eve::inventory::EquipmentSet equip;
    equip.setId("hero");
    equip.defineSlot("weapon");
    equip.defineSlot("armor");

    // 未装备：sync 不应产生加成
    CHECK_EQ(rpg->syncEquipModifiers(actor, &equip), 0);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 10.0));

    // 直接往武器槽塞物品（模拟已装备 sword）
    equip.stackAt("weapon")->itemId = "sword";
    equip.stackAt("weapon")->quantity = 1;
    int applied = rpg->syncEquipModifiers(actor, &equip);
    CHECK_EQ(applied, 2);
    // 10 + 15 = 25，再 * (1 + 0.1) = 27.5
    CHECK(approxEq(actor->getFinalAttribute("attack"), 27.5));

    // 换装/卸下后重新 sync 应移除旧加成
    equip.stackAt("weapon")->clear();
    CHECK_EQ(rpg->syncEquipModifiers(actor, &equip), 0);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 10.0));

    rpg->clearAllItemStats();
    actor->release();
}

TEST_CASE("rpg.loot.rollGuaranteedAndSeeded") {
    auto *rpg = RPG::create();
    rpg->clearLootTables();
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition gold;
    gold.id = "gold";
    gold.displayName = "Gold";
    gold.maxStack = 999;
    eve::inventory::ItemDefinition potion;
    potion.id = "potion";
    potion.displayName = "Potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(gold);
    eve::inventory::ItemRegistry::registerItem(potion);

    int tables = rpg->registerLootTablesFromJson(R"([
      {"id":"goblin","entries":[
        {"itemId":"gold","chance":1.0,"minQty":5,"maxQty":5},
        {"itemId":"potion","chance":0.0,"minQty":1,"maxQty":1}
      ]}
    ])");
    CHECK_EQ(tables, 1);
    CHECK_EQ(rpg->getLootTableCount(), 1);

    eve::inventory::Bag bag(20);
    bag.setId("player");
    int drops = rpg->rollLoot("goblin", &bag, 42);
    CHECK_EQ(drops, 1);  // 只有 gold 必掉
    CHECK_EQ(bag.countItem("gold"), 5);
    CHECK_EQ(bag.countItem("potion"), 0);

    // 未知表返回 0
    eve::inventory::Bag bag2(20);
    int missing = rpg->rollLoot("missing", &bag2, 42);
    CHECK_EQ(missing, 0);

    rpg->clearLootTables();
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("rpg.loot.rollDirectWithRng") {
    LootSystem::clear();
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition a;
    a.id = "a";
    a.displayName = "A";
    a.maxStack = 999;
    eve::inventory::ItemRegistry::registerItem(a);
    int tables = LootSystem::registerTablesFromJson(R"([
      {"id":"t","entries":[{"itemId":"a","chance":1.0,"minQty":2,"maxQty":2}]}
    ])");
    CHECK_EQ(tables, 1);
    eve::inventory::Bag bag(20);
    std::mt19937 rng(7);
    int rolls = LootSystem::roll("t", &bag, rng);
    CHECK_EQ(rolls, 1);
    CHECK_EQ(bag.countItem("a"), 2);
    LootSystem::clear();
    eve::inventory::ItemRegistry::clear();
}