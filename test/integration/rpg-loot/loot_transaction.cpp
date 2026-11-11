// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"rpg"}
*/
// clang-format on
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/LootSystem.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace {
struct LootFixture {
    LootFixture() {
        eve::rpg::LootSystem::clear();
        eve::inventory::ItemRegistry::clear();
        eve::inventory::InventorySystem::clearEvents();
        for (const auto *id : {"sword", "gem"}) {
            eve::inventory::ItemDefinition item;
            item.id       = id;
            item.maxStack = 1;
            eve::inventory::ItemRegistry::registerItem(item);
        }
        eve::rpg::LootSystem::registerTable({"reward", {{"sword", 1.0, 1, 1}, {"gem", 1.0, 1, 1}}});
    }
    ~LootFixture() {
        eve::inventory::InventorySystem::unregisterChangeHook("loot.test");
        eve::inventory::InventorySystem::clearEvents();
        eve::inventory::ItemRegistry::clear();
        eve::rpg::LootSystem::clear();
    }
};
}  // namespace

TEST_CASE("Loot batch rejects full inventory without partial grants or random consumption") {
    LootFixture         fixture;
    eve::inventory::Bag bag(1);
    std::mt19937        rng(42);
    const auto          before   = rng;
    const int           rejected = eve::rpg::LootSystem::roll("reward", &bag, rng);
    REQUIRE_EQ(rejected, 0);
    CHECK_EQ(bag.countItem("sword"), 0);
    CHECK_EQ(bag.countItem("gem"), 0);
    CHECK(rng == before);
    CHECK(eve::inventory::InventorySystem::events().empty());
    bag.setSlotCount(2);
    const int retried = eve::rpg::LootSystem::roll("reward", &bag, rng);
    CHECK_EQ(retried, 2);
    CHECK_EQ(bag.countItem("sword"), 1);
    CHECK_EQ(bag.countItem("gem"), 1);
    CHECK(rng != before);
}

TEST_CASE("Loot hooks observe the complete batch and may reload definitions") {
    LootFixture         fixture;
    eve::inventory::Bag bag(2);
    std::mt19937        rng(17);
    const auto          before   = rng;
    int                 calls    = 0;
    bool                complete = true;
    eve::inventory::InventorySystem::registerChangeHook("loot.test", [&](const auto &) {
        ++calls;
        complete = complete && bag.countItem("sword") == 1 && bag.countItem("gem") == 1 && rng != before;
        eve::rpg::LootSystem::clear();
    });
    const int result = eve::rpg::LootSystem::roll("reward", &bag, rng);
    CHECK_EQ(result, 2);
    CHECK_EQ(calls, 2);
    REQUIRE(complete);
}

TEST_CASE("Loot rejects malformed native definitions and unknown granted items atomically") {
    LootFixture         fixture;
    eve::inventory::Bag bag(5);
    std::mt19937        rng(9);
    const auto          before = rng;
    for (const auto &entry : std::vector<eve::rpg::LootEntry>{{"gem", std::numeric_limits<double>::quiet_NaN(), 1, 1},
                                                              {"gem", 1.1, 1, 1},
                                                              {"gem", -0.1, 1, 1},
                                                              {"gem", 1.0, 2, 1},
                                                              {"gem", 1.0, 0, 1},
                                                              {"missing", 1.0, 1, 1}}) {
        eve::rpg::LootSystem::registerTable({"invalid", {{"sword", 1.0, 1, 1}, entry}});
        const int result = eve::rpg::LootSystem::roll("invalid", &bag, rng);
        REQUIRE_EQ(result, 0);
        CHECK_EQ(bag.countItem("sword"), 0);
        CHECK(rng == before);
        CHECK(eve::inventory::InventorySystem::events().empty());
    }
}

TEST_CASE("Loot empty successful draws advance random stream without inventory events") {
    LootFixture         fixture;
    eve::inventory::Bag bag(0);
    std::mt19937        rng(9);
    const auto          before = rng;
    eve::rpg::LootSystem::registerTable({"empty", {{"gem", 0.0, 1, 1}}});
    const int result = eve::rpg::LootSystem::roll("empty", &bag, rng);
    REQUIRE_EQ(result, 0);
    CHECK(rng != before);
    CHECK(eve::inventory::InventorySystem::events().empty());
}

TEST_CASE("Loot weighted selection grants exactly one entry atomically") {
    LootFixture                   fixture;
    eve::inventory::Bag           bag(4);
    std::mt19937                  rng(11);
    eve::rpg::LootTableDefinition table;
    table.id         = "weighted";
    table.selectMode = eve::rpg::LootSelectMode::Weighted;
    table.entries    = {{"sword", 0.0, 1, 1, 1.0, false}, {"gem", 0.0, 1, 1, 3.0, false}};
    eve::rpg::LootSystem::registerTable(table);
    auto detailed = eve::rpg::LootSystem::rollDetailed("weighted", &bag, rng, {});
    REQUIRE(detailed.ok());
    CHECK_EQ(detailed.value(), 1);
    CHECK_EQ(bag.countItem("sword") + bag.countItem("gem"), 1);
}

TEST_CASE("Loot unique entries skip when bag already owns the item") {
    LootFixture         fixture;
    eve::inventory::Bag bag(4);
    REQUIRE_EQ(bag.addItem("sword", 1), 1);
    std::mt19937        rng(3);
    eve::rpg::LootEntry uniqueSword{"sword", 1.0, 1, 1, 0.0, true};
    eve::rpg::LootSystem::registerTable({"unique", {uniqueSword, {"gem", 1.0, 1, 1}}});
    auto detailed = eve::rpg::LootSystem::rollDetailed("unique", &bag, rng, {});
    REQUIRE(detailed.ok());
    CHECK_EQ(detailed.value(), 1);
    CHECK_EQ(bag.countItem("sword"), 1);
    CHECK_EQ(bag.countItem("gem"), 1);
}

TEST_CASE("Loot luck boosts independent chances without mutating on invalid luck") {
    LootFixture         fixture;
    eve::inventory::Bag bag(4);
    std::mt19937        rng(5);
    const auto          before = rng;
    eve::rpg::LootSystem::registerTable({"rare", {{"gem", 0.25, 1, 1}}});
    auto badLuck = eve::rpg::LootSystem::rollDetailed("rare", &bag, rng, {.luck = -2.0});
    CHECK(!badLuck.ok());
    CHECK(rng == before);
    CHECK_EQ(bag.countItem("gem"), 0);
    // luck=3 scales 0.25 → 1.0 so the Bernoulli always hits for this seed path.
    auto boosted = eve::rpg::LootSystem::rollDetailed("rare", &bag, rng, {.luck = 3.0});
    REQUIRE(boosted.ok());
    CHECK_EQ(boosted.value(), 1);
    CHECK_EQ(bag.countItem("gem"), 1);
}

TEST_CASE("Loot JSON weighted unique tables register through selectMode") {
    LootFixture fixture;
    REQUIRE_EQ(eve::rpg::LootSystem::registerTablesFromJson(R"([
      {"id":"json.weighted","selectMode":"weighted","entries":[
        {"itemId":"sword","weight":1,"minQty":1,"maxQty":1,"unique":true},
        {"itemId":"gem","weight":1,"minQty":1,"maxQty":1}
      ]}
    ])"),
               1);
    const auto *table = eve::rpg::LootSystem::find("json.weighted");
    REQUIRE(table != nullptr);
    CHECK(table->selectMode == eve::rpg::LootSelectMode::Weighted);
    CHECK(table->entries[0].unique);
    CHECK_EQ(table->entries[0].weight, 1.0);
}
