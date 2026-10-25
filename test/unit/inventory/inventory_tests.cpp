#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_inventory_gameplay_control)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayControlJson.h"
#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// inventory_gameplay_control.cpp
namespace inventory_control_contract {
constexpr const char* instanceId      = "00000000-0000-7000-8000-000000000701";
constexpr const char* ownerId         = "00000000-0000-7000-8000-000000000702";
constexpr const char* strangerId      = "00000000-0000-7000-8000-000000000703";
constexpr const char* otherInstanceId = "00000000-0000-7000-8000-000000000704";
constexpr const char* otherOwnerId    = "00000000-0000-7000-8000-000000000705";

eve::SubjectRef subject(const char* text) {
    auto parsed = eve::PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

eve::LogicalId action(const char* text) {
    auto parsed = eve::LogicalId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

struct Fixture {
    eve::inventory::Inventory*                 module    = nullptr;
    eve::inventory::Bag*                       bag       = nullptr;
    eve::inventory::EquipmentSet*              equipment = nullptr;
    eve::SubjectRef                            instance  = subject(instanceId);
    eve::SubjectRef                            owner     = subject(ownerId);
    std::vector<eve::inventory::Bag*>          bags;
    std::vector<eve::inventory::EquipmentSet*> sets;

    Fixture() {
        module = eve::inventory::Inventory::create();
        REQUIRE(module != nullptr);
        module->clearGameplayControls();
        module->clearItemDefinitions();
        REQUIRE(module->registerItemsFromJson(R"([
          {"id":"sword","maxStack":1,"weight":3,"tags":["weapon"],"equipSlot":"weapon"},
          {"id":"potion","maxStack":10,"weight":0.5,"tags":["potion"]}
        ])") == 2);
        bag = module->newBag(6);
        bags.push_back(bag);
        bag->setId("hero.bag");
        equipment = module->newEquipmentSet();
        sets.push_back(equipment);
        equipment->defineSlot("weapon");
        equipment->addSlotAllowedTag("weapon", "weapon");
        REQUIRE(module->publishGameplay(instanceId, ownerId, bag, equipment).ok());
    }

    ~Fixture() {
        module->clearGameplayControls();
        for (auto* set : sets) set->destroy();
        for (auto* container : bags) container->destroy();
    }

    eve::IGameplayControlProvider* provider() const {
        eve::IGameplayControlProvider* found = nullptr;
        eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
            if (candidate && candidate->gameplayDomain() == "inventory") found = candidate;
        });
        return found;
    }

    eve::GameplayCommand command(const char* id, const char* actionId, std::uint64_t revision,
                                 eve::Value parameters) const {
        eve::GameplayCommand result;
        result.id               = id;
        result.action           = action(actionId);
        result.subject          = owner;
        result.observedTick     = eve::SimulationTick::zero();
        result.expectedRevision = revision;
        result.parameters       = std::move(parameters);
        return result;
    }
};

eve::Value fields(std::initializer_list<std::pair<const std::string, eve::Value>> entries) {
    return eve::Value(eve::Value::Object(entries));
}

std::int64_t quantity(const eve::Value& value) {
    const auto* member = value.find("quantity");
    return member ? member->asInt() : -1;
}

TEST_CASE("inventory.control.publishedInstanceExposesScopedVocabularyAndObservation") {
    Fixture f;
    auto*   provider = f.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {f.owner}};
    eve::GameplaySession driver{"driver", eve::GameplayAccess::TestDriver, {f.owner}};
    auto                 observed = provider->observeGameplay(player, f.instance);
    REQUIRE(observed.ok());
    CHECK_EQ(observed.value().domain.format(), std::string("gameplay:inventory"));
    CHECK_EQ(observed.value().revision, std::uint64_t{0});
    CHECK_EQ(observed.value().state.find("bagId")->asString(), std::string("hero.bag"));
    auto playerActions = provider->availableGameplayActions(player, f.instance, f.owner);
    auto driverActions = provider->availableGameplayActions(driver, f.instance, f.owner);
    REQUIRE(playerActions.ok());
    REQUIRE(driverActions.ok());
    CHECK_EQ(playerActions.value().size(), std::size_t{4});
    CHECK_EQ(driverActions.value().size(), std::size_t{5});
    for (const auto& descriptor : playerActions.value())
        CHECK(descriptor.id.format() != std::string("inventory:add-item"));
    auto stranger = provider->observeGameplay(
        eve::GameplaySession{"stranger", eve::GameplayAccess::PlayerEquivalent, {subject(strangerId)}}, f.instance);
    CHECK(!stranger.ok());
    CHECK_EQ(stranger.code(), eve::StatusCode::Rejected);
}

TEST_CASE("inventory.control.oneProviderServesSeveralIndependentInstances") {
    Fixture f;
    auto*   other = f.module->newBag(4);
    f.bags.push_back(other);
    other->setId("companion.bag");
    REQUIRE(f.module->publishGameplay(otherInstanceId, otherOwnerId, other, nullptr).ok());
    CHECK_EQ(f.module->gameplayControlCount(), 2);
    int providerCount = 0;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* candidate) {
        if (candidate && candidate->gameplayDomain() == "inventory") ++providerCount;
    });
    CHECK_EQ(providerCount, 1);
    auto duplicate = f.module->publishGameplay(instanceId, ownerId, f.bag, f.equipment);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    eve::GameplaySession otherPlayer{"other", eve::GameplayAccess::PlayerEquivalent, {subject(otherOwnerId)}};
    auto                 observed = f.provider()->observeGameplay(otherPlayer, subject(otherInstanceId));
    REQUIRE(observed.ok());
    CHECK_EQ(observed.value().state.find("bagId")->asString(), std::string("companion.bag"));
    REQUIRE(f.module->unpublishGameplay(otherInstanceId).ok());
    CHECK(!f.provider()->observeGameplay(otherPlayer, subject(otherInstanceId)).ok());
    CHECK_EQ(f.module->gameplayControlCount(), 1);
}

TEST_CASE("inventory.control.grantPermissionRevisionAndEventQuantityAreAuthoritative") {
    Fixture f;
    auto*   provider = f.provider();
    REQUIRE(provider != nullptr);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {f.owner}};
    eve::GameplaySession driver{"driver", eve::GameplayAccess::TestDriver, {f.owner}};
    auto                 grant = [&](const char* id, std::uint64_t revision, int amount) {
        return f.command(id, "inventory:add-item", revision,
                                         fields({{"itemId", eve::Value("potion")}, {"quantity", eve::Value(std::int64_t{amount})}}));
    };
    auto denied = provider->submitGameplay(player, f.instance, grant("denied", 0, 2));
    CHECK(!denied.ok());
    CHECK_EQ(f.bag->countItem("potion"), 0);
    auto accepted = provider->submitGameplay(driver, f.instance, grant("accepted", 0, 2));
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value().resultingRevision, std::uint64_t{1});
    CHECK_EQ(quantity(accepted.value().details), std::int64_t{2});
    CHECK_EQ(f.bag->countItem("potion"), 2);
    auto stale = provider->submitGameplay(driver, f.instance, grant("stale", 0, 1));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK_EQ(f.bag->countItem("potion"), 2);
    auto events = provider->gameplayEvents(driver, f.instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value()[0].type, std::string("inventory.item-added"));
    CHECK_EQ(events.value()[0].causationCommandId, std::string("accepted"));
    CHECK_EQ(quantity(events.value()[0].payload), std::int64_t{2});
}

TEST_CASE("inventory.control.playerEquipUnequipAndPartialRemoveShareRevisionLedger") {
    Fixture f;
    REQUIRE(f.bag->addItem("sword", 1) == 1);
    REQUIRE(f.bag->addItem("potion", 3) == 3);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {f.owner}};
    auto*                provider = f.provider();
    REQUIRE(provider != nullptr);
    auto equip = provider->submitGameplay(
        player, f.instance,
        f.command("equip", "inventory:equip", 0,
                  fields({{"bagSlot", eve::Value(std::int64_t{0})}, {"equipSlot", eve::Value("weapon")}})));
    REQUIRE(equip.ok());
    CHECK_EQ(f.equipment->getSlotItemId("weapon"), std::string("sword"));
    auto unequip = provider->submitGameplay(
        player, f.instance,
        f.command("unequip", "inventory:unequip", 1, fields({{"equipSlot", eve::Value("weapon")}})));
    REQUIRE(unequip.ok());
    CHECK(f.equipment->isSlotEmpty("weapon"));
    auto remove = [&](const char* id, std::uint64_t revision, int amount) {
        return f.command(id, "inventory:remove-item", revision,
                         fields({{"itemId", eve::Value("potion")}, {"quantity", eve::Value(std::int64_t{amount})}}));
    };
    auto partial = provider->submitGameplay(player, f.instance, remove("remove", 2, 5));
    REQUIRE(partial.ok());
    CHECK_EQ(quantity(partial.value().details), std::int64_t{3});
    CHECK_EQ(f.bag->countItem("potion"), 0);
    CHECK(!provider->submitGameplay(player, f.instance, remove("empty", 3, 1)).ok());
    auto events = provider->gameplayEvents(player, f.instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{3});
    CHECK_EQ(events.value()[2].type, std::string("inventory.item-removed"));
    CHECK_EQ(quantity(events.value()[2].payload), std::int64_t{3});
}

// inventory_save_session.cpp
TEST_CASE("inventory.saveSession.prepareCommitAndFailedRestoreAreAtomic") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::InventorySystem::ensureBuiltins();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag sourceBag(4);
    sourceBag.setId("source");
    REQUIRE(sourceBag.addItem("potion", 3) == 3);
    eve::inventory::EquipmentSet sourceEquipment;
    sourceEquipment.defineSlot("weapon");
    eve::inventory::InventorySaveSession source;
    source.bind(sourceBag, sourceEquipment);
    auto saved = source.snapshotJson();
    REQUIRE(saved.ok());
    CHECK(saved.value().find("eve.inventory.save-session") != std::string::npos);

    eve::inventory::Bag                  destinationBag(1);
    eve::inventory::EquipmentSet         destinationEquipment;
    eve::inventory::InventorySaveSession destination;
    destination.bind(destinationBag, destinationEquipment);
    auto prepared = destination.prepareRestoreSnapshotJson(saved.value());
    REQUIRE(prepared.ok());
    CHECK_EQ(destinationBag.countItem("potion"), 0);
    destination.commitPrepared(std::move(prepared).takeValue());
    CHECK_EQ(destinationBag.countItem("potion"), 3);
    auto before = destination.snapshotJson();
    REQUIRE(before.ok());
    CHECK(!destination.restoreSnapshotJson("{}").ok());
    CHECK_EQ(destination.snapshotJson().value(), before.value());
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("inventory.saveSession.unboundSessionRejectsReadAndRestore") {
    eve::inventory::InventorySaveSession session;
    CHECK(!session.snapshotJson().ok());
    CHECK(!session.prepareRestoreSnapshotJson("{}").ok());
    REQUIRE(!session.restoreSnapshotJson("{}").ok());
}
}  // namespace inventory_control_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_inventory)
#include "PrivateTestScope.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

// inventory.cpp
namespace inventory_core_contract {
using namespace eve::inventory;

struct Catalog {
    Catalog() {
        ItemRegistry::clear();
        InventorySystem::clearEvents();
        InventorySystem::ensureBuiltins();
        ItemDefinition coin;
        coin.id       = "coin";
        coin.maxStack = 10;
        coin.weight   = .5f;
        coin.tags     = {"currency"};
        ItemRegistry::registerItem(coin);
        ItemDefinition sword;
        sword.id        = "sword";
        sword.maxStack  = 1;
        sword.equipSlot = "weapon";
        sword.tags      = {"weapon"};
        ItemRegistry::registerItem(sword);
    }
    ~Catalog() { ItemRegistry::clear(); }
};

TEST_CASE("inventory.catalog.registerLookupLoadAndClear") {
    Catalog catalog;
    REQUIRE(ItemRegistry::find("coin") != nullptr);
    CHECK_EQ(ItemRegistry::find("coin")->maxStack, 10);
    CHECK(ItemRegistry::find("coin")->hasTag("currency"));
    CHECK(ItemRegistry::find("unknown") == nullptr);
    const int loaded = ItemRegistry::loadFromJson(R"([{"id":"ore","maxStack":50,"weight":1,"tags":["material"]}])");
    CHECK_EQ(loaded, 1);
    CHECK_EQ(ItemRegistry::count(), 3);
    ItemRegistry::clear();
    CHECK_EQ(ItemRegistry::count(), 0);
}

TEST_CASE("inventory.bag.stacksAndRemovesInStableSlotOrder") {
    Catalog catalog;
    Bag bag(4);
    bag.setId("hero");
    REQUIRE(bag.addItem("coin", 15) == 15);
    CHECK_EQ(bag.getUsedSlotCount(), 2);
    CHECK_EQ(bag.getSlotQuantity(0), 10);
    CHECK_EQ(bag.getSlotQuantity(1), 5);
    REQUIRE(bag.removeItem("coin", 7) == 7);
    CHECK_EQ(bag.getSlotQuantity(0), 3);
    CHECK_EQ(bag.getSlotQuantity(1), 5);
    CHECK_EQ(bag.countItem("coin"), 8);
    CHECK(bag.removeItem("coin", 99) == 8);
    CHECK_EQ(bag.countItem("coin"), 0);
}

TEST_CASE("inventory.bag.capacityAndTagsRejectInvalidAdds") {
    Catalog catalog;
    Bag     weighted(4);
    weighted.setMaxWeight(1.f);
    weighted.setCapacityPolicy("slotsAndWeight");
    CHECK_EQ(weighted.addItem("coin", 3), 2);
    CHECK_EQ(weighted.getUsedWeight(), 1.f);
    CHECK(!weighted.canAddItem("coin", 1));
    CHECK_EQ(weighted.canAddItemReason("coin", 1), std::string("over_weight"));
    Bag filtered(4);
    filtered.addAcceptTag("weapon");
    CHECK(!filtered.canAddItem("coin", 1));
    CHECK_EQ(filtered.canAddItemReason("coin", 1), std::string("accept_tag_mismatch"));
    CHECK(filtered.canAddItem("sword", 1));
    filtered.addRejectTag("weapon");
    REQUIRE(!filtered.canAddItem("sword", 1));
}

TEST_CASE("inventory.bag.splitMoveSwapAndTransferConserveQuantity") {
    Catalog catalog;
    Bag     source(4);
    Bag     destination(4);
    REQUIRE(source.addItem("coin", 8) == 8);
    REQUIRE(source.splitStack(0, 3, 1));
    CHECK_EQ(source.getSlotQuantity(0), 5);
    CHECK_EQ(source.getSlotQuantity(1), 3);
    REQUIRE(source.moveSlot(1, 2));
    CHECK(source.isSlotEmpty(1));
    REQUIRE(source.swapSlots(0, 2));
    CHECK_EQ(source.getSlotQuantity(0), 3);
    CHECK_EQ(source.getSlotQuantity(2), 5);
    REQUIRE(source.moveSlot(0, 2));
    CHECK_EQ(source.getSlotQuantity(2), 8);
    const int moved = InventorySystem::transfer(&source, &destination, "coin", 6);
    CHECK_EQ(moved, 6);
    CHECK_EQ(source.countItem("coin"), 2);
    CHECK_EQ(destination.countItem("coin"), 6);
}

TEST_CASE("inventory.equipment.rejectsWrongSlotAndReturnsItemOnUnequip") {
    Catalog catalog;
    Bag bag(4);
    REQUIRE(bag.addItem("sword", 1) == 1);
    REQUIRE(bag.addItem("coin", 1) == 1);
    EquipmentSet equipment;
    equipment.defineSlot("weapon");
    equipment.addSlotAllowedTag("weapon", "weapon");
    CHECK(!equipment.equipFromBag("weapon", &bag, bag.findItem("coin")));
    REQUIRE(equipment.equipFromBag("weapon", &bag, bag.findItem("sword")));
    CHECK_EQ(equipment.getSlotItemId("weapon"), std::string("sword"));
    CHECK_EQ(bag.countItem("sword"), 0);
    REQUIRE(equipment.unequipToBag("weapon", &bag));
    CHECK(equipment.isSlotEmpty("weapon"));
    CHECK_EQ(bag.countItem("sword"), 1);
}

TEST_CASE("inventory.facade.exposesDefinitionsAndObservableChangeEvents") {
    Catalog catalog;
    auto*   inventory = Inventory::create();
    REQUIRE(inventory != nullptr);
    auto* bag = inventory->newBag(4);
    bag->setId("pack");
    inventory->clearChangeEvents();
    REQUIRE(bag->addItem("coin", 4) == 4);
    CHECK(inventory->getChangeEventCount() >= 1);
    bool foundAdd = false;
    for (int i = 0; i < inventory->getChangeEventCount(); ++i)
        if (inventory->getChangeEventAction(i) == "add" && inventory->getChangeEventItemId(i) == "coin")
            foundAdd = true;
    CHECK(foundAdd);
    inventory->clearChangeEvents();
    CHECK_EQ(inventory->getChangeEventCount(), 0);
    bag->destroy();
}
}  // namespace inventory_core_contract
#endif
