#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve::inventory;

TEST_CASE("inventory.protobufSave.roundTripAndAtomicFailure") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition potion;
    potion.id       = "proto.potion";
    potion.maxStack = 20;
    ItemRegistry::registerItem(potion);

    Bag sourceBag(2);
    sourceBag.setId("source");
    const int added = sourceBag.addItem("proto.potion", 3);
    REQUIRE(added == 3);
    EquipmentSet sourceEquipment;
    sourceEquipment.setId("equipment");
    sourceEquipment.defineSlot("weapon");
    InventorySaveSession source;
    source.bind(sourceBag, sourceEquipment);
    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());

    Bag                  restoredBag(1);
    EquipmentSet         restoredEquipment;
    InventorySaveSession restored;
    restored.bind(restoredBag, restoredEquipment);
    REQUIRE(restored.restoreSnapshotBinary(bytes.value()).ok());
    CHECK_EQ(restoredBag.getId(), std::string("source"));
    CHECK_EQ(restoredBag.countItem("proto.potion"), 3);
    CHECK_EQ(restoredEquipment.getId(), std::string("equipment"));

    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    Bag                  jsonBag(0);
    EquipmentSet         jsonEquipment;
    InventorySaveSession jsonRestore;
    jsonRestore.bind(jsonBag, jsonEquipment);
    REQUIRE(jsonRestore.restoreSnapshotJson(json.value()).ok());
    CHECK_EQ(jsonBag.countItem("proto.potion"), 3);

    auto before = restored.snapshotBinary();
    REQUIRE(before.ok());
    std::string malformed = bytes.value();
    malformed.push_back(static_cast<char>(0xff));
    CHECK(!restored.restoreSnapshotBinary(malformed).ok());
    std::string unknown = bytes.value();
    unknown.append("\x98\x06\x01", 3);  // Field 99, varint 1.
    CHECK(!restored.restoreSnapshotBinary(unknown).ok());
    auto after = restored.snapshotBinary();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    ItemRegistry::clear();
}

TEST_CASE("inventory.protobufSave.readsLegacyJsonAndRejectsFutureVersion") {
    Bag                  bag(0);
    EquipmentSet         equipment;
    InventorySaveSession session;
    session.bind(bag, equipment);
    constexpr auto legacy = R"({
      "schema":"eve.inventory.save-session","version":1,
      "bag":{"id":"old","kind":"backpack","maxWeight":0,"maxVolume":0,
             "acceptRule":"default","capacityPolicy":"slotsAndWeight","stackRule":"sameItem",
             "acceptTags":[],"rejectTags":[],"extra":{},"slots":[]},
      "equipment":{"id":"old-equipment","slots":[]}
    })";
    REQUIRE(session.restoreSnapshotJson(legacy).ok());
    CHECK_EQ(bag.getId(), std::string("old"));
    auto before = session.snapshotBinary();
    REQUIRE(before.ok());
    std::string future(legacy);
    const auto  position = future.find("\"version\":1");
    REQUIRE(position != std::string::npos);
    future.replace(position, std::string("\"version\":1").size(), "\"version\":2");
    CHECK(!session.restoreSnapshotJson(future).ok());
    auto after = session.snapshotBinary();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
}
