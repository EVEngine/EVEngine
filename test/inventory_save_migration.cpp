#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

TEST_CASE("inventory.protobufSave.rewritesContentIdsWithoutLiveMutation") {
    using namespace eve::inventory;
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition oldItem;
    oldItem.id       = "item.old";
    oldItem.maxStack = 20;
    ItemRegistry::registerItem(oldItem);
    Bag bag(2);
    REQUIRE_EQ(bag.addItem("item.old", 3), 3);
    EquipmentSet equipment;
    equipment.defineSlot("weapon.old");
    InventorySaveSession source;
    source.bind(bag, equipment);
    auto original = source.snapshotBinary();
    REQUIRE(original.ok());

    auto renamedItem = InventorySaveSession::rewriteSnapshotBinaryId(original.value(), InventorySaveIdKind::Item,
                                                                     "item.old", "item.new");
    REQUIRE(renamedItem.ok());
    auto renamedSlot = InventorySaveSession::rewriteSnapshotBinaryId(
        renamedItem.value(), InventorySaveIdKind::EquipmentSlot, "weapon.old", "weapon.new");
    REQUIRE(renamedSlot.ok());
    CHECK_EQ(bag.countItem("item.old"), 3);

    ItemRegistry::clear();
    ItemDefinition newItem;
    newItem.id       = "item.new";
    newItem.maxStack = 20;
    ItemRegistry::registerItem(newItem);
    Bag                  restoredBag(0);
    EquipmentSet         restoredEquipment;
    InventorySaveSession restored;
    restored.bind(restoredBag, restoredEquipment);
    REQUIRE(restored.restoreSnapshotBinary(renamedSlot.value()).ok());
    CHECK_EQ(restoredBag.countItem("item.new"), 3);
    CHECK(restoredEquipment.hasSlot("weapon.new"));
    CHECK(!InventorySaveSession::rewriteSnapshotBinaryId("malformed", InventorySaveIdKind::Item, "item.old", "item.new")
               .ok());
    ItemRegistry::clear();
}
