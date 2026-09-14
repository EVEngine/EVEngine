#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/ProductControl.h"
#include "rpg/Quest.h"
#include "rpg/RPG.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

void installProductFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::InventorySystem::clearEvents();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
    eve::inventory::ItemDefinition item;
    item.id = "cache_item";
    item.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(item);
    auto shop = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.cache","itemId":"cache_item","name":"Cache Item","desc":"","buyPrice":20,"sellPrice":10}])");
    REQUIRE(shop.ok());
    eve::rpg::QuestDefinition quest;
    quest.id = "quest.cache";
    quest.startPolicy = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"recover", "interact", "cache", 1});
    quest.rewards.push_back({"item", "cache_item", 2.0});
    quest.rewards.push_back({"attribute", "gold", 5.0});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

void clearProductFixture() {
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
}

eve::GameplayCommand command(const char* id, const char* actionId,
                             eve::SubjectRef instance,
                             const eve::GameplayObservation& observation,
                             eve::Value parameters) {
    eve::GameplayCommand result;
    result.id = id;
    result.action = action(actionId);
    result.subject = instance;
    result.observedTick = observation.tick;
    result.expectedRevision = observation.revision;
    result.parameters = std::move(parameters);
    return result;
}

}  // namespace

TEST_CASE("gameplay.control.rpgProductRunsShopLootAndQuestThroughCanonicalTransactions") {
    installProductFixture();
    const auto instance = subject("00000000-0000-7000-8000-000000000701");
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.cache"));
    eve::inventory::Bag bag(4);
    bag.setId("hero");
    eve::rpg::ProductControl control(instance, state, tracker, bag);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {instance}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {instance}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "rpg.product") discovered = true;
    });
    CHECK(discovered);
    auto playerActions = control.availableGameplayActions(player, instance, instance);
    auto automationActions = control.availableGameplayActions(automation, instance, instance);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerDescriptors = std::move(playerActions).takeValue();
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(playerDescriptors.size(), std::size_t{4});
    REQUIRE_EQ(automationDescriptors.size(), playerDescriptors.size());
    for (std::size_t index = 0; index < playerDescriptors.size(); ++index)
        CHECK_EQ(automationDescriptors[index].id, playerDescriptors[index].id);

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeBuy = std::move(observed).takeValue();
    auto buy = command("product-buy-1", "rpg:buy-offer", instance, beforeBuy,
                       eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                     {"offerId", eve::Value("village.cache")},
                                                     {"quantity", eve::Value(2)}}));
    auto bought = control.submitGameplay(player, instance, buy);
    REQUIRE(bought.ok());
    CHECK_EQ(state.getVariable("gold"), 60.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    auto stale = command("product-stale", "rpg:sell-offer", instance, beforeBuy,
                         eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                       {"offerId", eve::Value("village.cache")},
                                                       {"quantity", eve::Value(1)}}));
    auto staleResult = control.submitGameplay(player, instance, stale);
    CHECK(!staleResult.ok());
    CHECK_EQ(staleResult.code(), eve::StatusCode::Conflict);
    CHECK_EQ(state.getVariable("gold"), 60.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeSell = std::move(observed).takeValue();
    auto sell = command("product-sell-1", "rpg:sell-offer", instance, beforeSell,
                        eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                      {"offerId", eve::Value("village.cache")},
                                                      {"quantity", eve::Value(1)}}));
    REQUIRE(control.submitGameplay(player, instance, sell).ok());
    CHECK_EQ(state.getVariable("gold"), 70.0);
    CHECK_EQ(bag.countItem("cache_item"), 1);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeLoot = std::move(observed).takeValue();
    auto loot = command(
        "product-loot-1", "rpg:collect-loot", instance, beforeLoot,
        eve::Value(eve::Value::Object{{"attributeAmount", eve::Value(7.0)},
                                      {"attributeId", eve::Value("gold")},
                                      {"itemId", eve::Value("cache_item")},
                                      {"itemQuantity", eve::Value(1)},
                                      {"mapId", eve::Value("forest")},
                                      {"notifyAmount", eve::Value(1)},
                                      {"notifyTarget", eve::Value("cache")},
                                      {"notifyTopic", eve::Value("interact")},
                                      {"objectId", eve::Value("cache")},
                                      {"requiredQuestId", eve::Value("quest.cache")}}));
    REQUIRE(control.submitGameplay(automation, instance, loot).ok());
    eve::rpg::WorldState world(state);
    CHECK(world.isObjectConsumed("forest", "cache"));
    CHECK_EQ(tracker.getState("quest.cache"), std::string("ready"));
    CHECK_EQ(state.getVariable("gold"), 77.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeClaim = std::move(observed).takeValue();
    auto claim = command("product-claim-1", "rpg:claim-quest", instance, beforeClaim,
                         eve::Value(eve::Value::Object{{"questId", eve::Value("quest.cache")}}));
    REQUIRE(control.submitGameplay(player, instance, claim).ok());
    CHECK_EQ(tracker.getState("quest.cache"), std::string("completed"));
    CHECK_EQ(state.getVariable("gold"), 82.0);
    CHECK_EQ(bag.countItem("cache_item"), 4);

    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    const auto batch = std::move(events).takeValue();
    REQUIRE_EQ(batch.size(), std::size_t{4});
    CHECK_EQ(batch.front().causationCommandId, std::string("product-buy-1"));
    CHECK_EQ(batch.back().type, std::string("rpg.product.claim-quest"));
    clearProductFixture();
}

TEST_CASE("gameplay.control.rpgProductDetectsMutationOutsideTheAdapter") {
    installProductFixture();
    const auto instance = subject("00000000-0000-7000-8000-000000000711");
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::rpg::Tracker tracker;
    eve::inventory::Bag bag(2);
    eve::rpg::ProductControl control(instance, state, tracker, bag);
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {instance}};
    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();

    state.addVariable("gold", 1.0);
    auto buy = command("external-stale", "rpg:buy-offer", instance, before,
                       eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                     {"offerId", eve::Value("village.cache")},
                                                     {"quantity", eve::Value(1)}}));
    auto rejected = control.submitGameplay(player, instance, buy);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK_EQ(state.getVariable("gold"), 101.0);
    CHECK_EQ(bag.countItem("cache_item"), 0);
    clearProductFixture();
}

TEST_CASE("gameplay.control.rpgControlFactoryReturnsStructuredOwnership") {
    const auto instance = subject("00000000-0000-7000-8000-000000000721");
    eve::rpg::RPG module;
    eve::rpg::GameState state;
    eve::rpg::Tracker tracker;
    eve::inventory::Bag bag(1);
    auto rejected = module.newProductControl(instance, nullptr, &tracker, &bag);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);

    auto created = module.newProductControl(instance, &state, &tracker, &bag);
    REQUIRE(created.ok());
    auto control = std::move(created).takeValue();
    REQUIRE(static_cast<bool>(control));
    CHECK_EQ(control->gameplayDomain(), std::string_view("rpg.product"));
}
