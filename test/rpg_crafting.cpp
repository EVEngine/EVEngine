#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/Crafting.h"

#include <limits>

using namespace eve;

TEST_CASE("rpg.craftingAlchemyPaysQueuesRetriesAndSettlesOnce") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::clearEvents();
    inventory::InventorySystem::ensureBuiltins();
    for (const auto& id : {"herb", "water", "healing_potion"}) {
        inventory::ItemDefinition item;
        item.id = id;
        item.maxStack = 99;
        inventory::ItemRegistry::registerItem(item);
    }

    inventory::Bag ingredients(4);
    ingredients.setId("player.ingredients");
    const int herbsAdded = ingredients.addItem("herb", 5);
    const int waterAdded = ingredients.addItem("water", 2);
    REQUIRE_EQ(herbsAdded, 5);
    REQUIRE_EQ(waterAdded, 2);
    inventory::InventoryResourceAccount account(ingredients);

    auto cost = resource::CostSpec::from({{"herb", 2}, {"water", 1}});
    REQUIRE(cost.ok());
    auto recipeRef = DefinitionRef::parse("recipe:healing_potion");
    REQUIRE(recipeRef.ok());
    rpg::CraftingRecipe recipe;
    recipe.id = "healing_potion";
    recipe.process = "alchemy";
    recipe.definition = {std::move(recipeRef).takeValue(), Generation(4)};
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs = {{"healing_potion", 2}};
    recipe.duration = Duration::fromSeconds(2.0).expect("craft duration");

    production::WorkQueue queue;
    rpg::CraftingRequest request;
    request.production = &queue;
    request.ingredients = &account;
    request.owner = "player:alchemy_table";
    request.recipe = recipe;
    request.batchSize = 2;
    auto begun = rpg::Crafting::begin(std::move(request));
    REQUIRE(begun.ok());
    const std::string taskId = begun.value().productionTaskId;
    CHECK_EQ(ingredients.countItem("herb"), 1);
    CHECK_EQ(ingredients.countItem("water"), 0);
    REQUIRE(queue.advance({SimulationTick(1), recipe.duration}).ok());
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::ReadyToSettle);
    CHECK_EQ(queue.find(taskId)->get().definition.generation.value(), std::uint64_t{4});
    CHECK_EQ(queue.find(taskId)->get().batchSize, std::uint32_t{2});

    inventory::Bag full(0);
    full.setId("full.output");
    auto blocked = rpg::Crafting::settle(queue, full, taskId);
    CHECK(!blocked.ok());
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::SettlementFailed);

    inventory::Bag output(2);
    output.setId("player.output");
    auto settled = rpg::Crafting::settle(queue, output, taskId);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value(), 4);
    CHECK_EQ(output.countItem("healing_potion"), 4);
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::Completed);

    auto repeated = rpg::Crafting::settle(queue, output, taskId);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.status().code(), StatusCode::NoOp);
    CHECK_EQ(output.countItem("healing_potion"), 4);
}

TEST_CASE("rpg.craftingRejectsInsufficientIngredientsWithoutQueueMutation") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition herb;
    herb.id = "herb";
    herb.maxStack = 99;
    inventory::ItemRegistry::registerItem(herb);
    inventory::ItemDefinition potion;
    potion.id = "healing_potion";
    potion.maxStack = 99;
    inventory::ItemRegistry::registerItem(potion);

    inventory::Bag bag(2);
    inventory::InventoryResourceAccount account(bag);
    auto cost = resource::CostSpec::single("herb", 1);
    REQUIRE(cost.ok());
    rpg::CraftingRecipe recipe;
    recipe.id = "healing_potion";
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs = {{"healing_potion", 1}};
    recipe.duration = Duration::fromSeconds(1.0).expect("craft duration");
    production::WorkQueue queue;
    rpg::CraftingRequest request{&queue, &account, "player", recipe};
    auto result = rpg::Crafting::begin(std::move(request));
    CHECK(!result.ok());
    CHECK_EQ(queue.taskCount(), 0);
}

TEST_CASE("rpg.craftingRejectsAggregateOutputOverflowBeforePayment") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition herb;
    herb.id = "herb";
    herb.maxStack = 99;
    inventory::ItemRegistry::registerItem(herb);
    inventory::Bag ingredients(1);
    REQUIRE_EQ(ingredients.addItem("herb", 1), 1);
    inventory::InventoryResourceAccount account(ingredients);
    auto cost = resource::CostSpec::single("herb", 1);
    REQUIRE(cost.ok());

    rpg::CraftingRecipe recipe;
    recipe.id = "overflowing_recipe";
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs = {{"first", std::numeric_limits<int>::max()}, {"second", 1}};
    recipe.duration = Duration::fromNanoseconds(1);
    production::WorkQueue queue;
    rpg::CraftingRequest request{&queue, &account, "player", recipe};
    auto result = rpg::Crafting::begin(std::move(request));
    CHECK(!result.ok());
    CHECK_EQ(ingredients.countItem("herb"), 1);
    CHECK_EQ(queue.taskCount(), 0);
}
