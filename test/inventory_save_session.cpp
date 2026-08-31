#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

using namespace eve::inventory;

TEST_CASE("inventory.saveSession.roundTripAndAtomicFailure") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition potion;
    potion.id = "potion";
    potion.maxStack = 20;
    potion.tags = {"consumable"};
    ItemRegistry::registerItem(potion);
    ItemDefinition sword;
    sword.id = "sword";
    sword.equipSlot = "weapon";
    sword.tags = {"weapon"};
    ItemRegistry::registerItem(sword);

    Bag sourceBag(4);
    sourceBag.setId("hero.bag");
    sourceBag.setMaxWeight(50.f);
    sourceBag.addItem("potion", 3);
    sourceBag.addItem("sword", 1);
    sourceBag.setSlotDurability(sourceBag.findItem("sword"), 0.75f);
    EquipmentSet sourceEquipment;
    sourceEquipment.setId("hero.equipment");
    sourceEquipment.defineSlot("weapon");
    sourceEquipment.addSlotAllowedTag("weapon", "weapon");
    REQUIRE(sourceEquipment.equipFromBag("weapon", &sourceBag, sourceBag.findItem("sword")));

    InventorySaveSession source;
    source.bind(sourceBag, sourceEquipment);
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    Bag restoredBag(1);
    restoredBag.addItem("potion", 1);
    EquipmentSet restoredEquipment;
    InventorySaveSession restored;
    restored.bind(restoredBag, restoredEquipment);
    REQUIRE(restored.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restoredBag.getId(), std::string("hero.bag"));
    CHECK_EQ(restoredBag.getSlotCount(), 4);
    CHECK_EQ(restoredBag.countItem("potion"), 3);
    CHECK_EQ(restoredEquipment.getId(), std::string("hero.equipment"));
    CHECK_EQ(restoredEquipment.getSlotItemId("weapon"), std::string("sword"));
    CHECK_EQ(restoredEquipment.stackAt("weapon")->durability, 0.75f);

    const auto before = restored.snapshotJson();
    REQUIRE(before.ok());
    auto parsed = eve::Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    eve::Value corrupted = std::move(parsed).takeValue();
    eve::Value *equipment = corrupted.find("equipment");
    REQUIRE(equipment != nullptr);
    eve::Value *slots = equipment->find("slots");
    REQUIRE(slots != nullptr);
    slots->at(0).find("stack")->set("itemId", eve::Value("missing.definition"));
    auto corruptedJson = corrupted.toJson();
    REQUIRE(corruptedJson.ok());
    CHECK(!restored.restoreSnapshotJson(corruptedJson.value()).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    ItemRegistry::clear();
}

TEST_CASE("inventory.saveSession.unboundOperationsFailStructurally") {
    InventorySaveSession session;
    CHECK(!session.snapshotJson().ok());
    CHECK(!session.restoreSnapshotJson("{}").ok());
}
