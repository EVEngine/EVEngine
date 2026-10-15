#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/ShopTransaction.h"

namespace {

void installShopFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemDefinition potion;
    potion.id = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
}

void clearShopFixture() {
    eve::inventory::InventorySystem::unregisterChangeHook("test.shop");
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
}

}  // namespace

TEST_CASE("rpg.shop.buyAndSellPublishOnlyCommittedFinalState") {
    installShopFixture();
    auto catalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(catalogue.ok());
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::inventory::Bag bag(2);
    bag.setId("hero");
    int hookCalls = 0;
    bool hookSawFinalState = true;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.shop", [&](const eve::inventory::InventoryChangeEvent &event) {
            ++hookCalls;
            if (event.action == "add")
                hookSawFinalState = hookSawFinalState && state.getVariable("gold") == 60.0 &&
                                    bag.countItem("potion") == 2;
            if (event.action == "remove")
                hookSawFinalState = hookSawFinalState && state.getVariable("gold") == 70.0 &&
                                    bag.countItem("potion") == 1;
        });

    auto bought = eve::rpg::ShopTransaction::buyOffer(state, bag, "gold", "village.potion", 2);
    REQUIRE(bought.ok());
    CHECK_EQ(std::move(bought).takeValue(), 2);
    auto sold = eve::rpg::ShopTransaction::sellOffer(state, bag, "gold", "village.potion", 1);
    REQUIRE(sold.ok());
    CHECK_EQ(std::move(sold).takeValue(), 1);
    CHECK_EQ(state.getVariable("gold"), 70.0);
    CHECK_EQ(bag.countItem("potion"), 1);
    CHECK_EQ(hookCalls, 2);
    CHECK(hookSawFinalState);
    clearShopFixture();
}

TEST_CASE("rpg.shop.failuresLeaveCurrencyInventoryAndObserversUntouched") {
    installShopFixture();
    auto catalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(catalogue.ok());
    eve::rpg::GameState state;
    state.setVariable("gold", 5.0);
    eve::inventory::Bag bag(0);
    int hookCalls = 0;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.shop", [&](const eve::inventory::InventoryChangeEvent &) { ++hookCalls; });

    auto insufficient = eve::rpg::ShopTransaction::buyOffer(
        state, bag, "gold", "village.potion", 1);
    REQUIRE(!insufficient.ok());
    auto missing = eve::rpg::ShopTransaction::sellOffer(
        state, bag, "gold", "village.potion", 1);
    REQUIRE(!missing.ok());
    CHECK_EQ(state.getVariable("gold"), 5.0);
    CHECK_EQ(bag.countItem("potion"), 0);
    CHECK_EQ(hookCalls, 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearShopFixture();
}

TEST_CASE("rpg.shop.catalogueStrictReplacementIsOrderedAndAtomic") {
    installShopFixture();
    auto initial = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(initial.ok());
    REQUIRE_EQ(eve::rpg::ShopCatalogue::count(), 1);
    REQUIRE(eve::rpg::ShopCatalogue::at(0) != nullptr);
    CHECK_EQ(eve::rpg::ShopCatalogue::at(0)->buyPrice, 20);

    auto invalid = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","itemId":"missing","name":"Bad","desc":"","buyPrice":1,"sellPrice":2}])");
    REQUIRE(!invalid.ok());
    CHECK_EQ(eve::rpg::ShopCatalogue::count(), 1);
    CHECK(eve::rpg::ShopCatalogue::find("village.potion") != nullptr);
    auto unknownField = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","itemId":"potion","name":"Bad","desc":"","buyPrice":1,"sellPrice":0,"typo":1}])");
    REQUIRE(!unknownField.ok());
    CHECK(eve::rpg::ShopCatalogue::find("village.potion") != nullptr);
    clearShopFixture();
}

TEST_CASE("inventory.removePreparedStateRejectsStaleBagWithoutPublishing") {
    installShopFixture();
    eve::inventory::Bag bag(2);
    REQUIRE_EQ(bag.addItem("potion", 2), 2);
    eve::inventory::InventorySystem::clearEvents();
    auto prepared = eve::inventory::InventorySystem::prepareRemove(&bag, "potion", 1);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(bag.addItem("potion", 1), 1);
    eve::inventory::InventorySystem::clearEvents();
    auto committed = eve::inventory::InventorySystem::commitRemove(std::move(prepared).takeValue());
    REQUIRE(!committed.ok());
    CHECK_EQ(bag.countItem("potion"), 3);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearShopFixture();
}
