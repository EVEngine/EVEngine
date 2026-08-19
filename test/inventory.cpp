#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Item.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>

using namespace eve::inventory;
using namespace eve::graphics;
using namespace eve::ui;

namespace {
bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }
}  // namespace

TEST_CASE("inventory.itemRegistry.registerAndJson") {
    ItemRegistry::clear();

    ItemDefinition potion;
    potion.id = "potion.hp";
    potion.displayName = "Heal Potion";
    potion.maxStack = 20;
    potion.weight = 0.2f;
    potion.tags = {"consumable", "potion"};
    potion.extra["icon"] = "ui/potion.png";
    ItemRegistry::registerItem(potion);

    CHECK(ItemRegistry::count() == 1);
    CHECK(ItemRegistry::find("potion.hp") != nullptr);
    CHECK_EQ(ItemRegistry::find("potion.hp")->maxStack, 20);
    CHECK(ItemRegistry::find("potion.hp")->hasTag("potion"));
    CHECK_EQ(ItemRegistry::find("potion.hp")->getExtra("icon"), "ui/potion.png");

    int n = ItemRegistry::loadFromJson(R"([
      {"id":"ore.iron","displayName":"Iron Ore","maxStack":50,"weight":1.0,"tags":["material"]},
      {"id":"sword.iron","displayName":"Iron Sword","maxStack":1,"weight":3.5,
       "equipSlot":"weapon","tags":["weapon","melee"],"extra":{"damage":"10"}}
    ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(ItemRegistry::count(), 3);
    CHECK_EQ(ItemRegistry::find("sword.iron")->equipSlot, "weapon");

    ItemRegistry::clear();
    CHECK_EQ(ItemRegistry::count(), 0);
}

TEST_CASE("inventory.bag.addStackRemove") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();

    ItemDefinition coin;
    coin.id = "coin";
    coin.maxStack = 99;
    coin.weight = 0.01f;
    ItemRegistry::registerItem(coin);

    Bag bag(5);
    bag.setId("player");
    bag.setMaxWeight(100.f);

    CHECK(bag.canAddItem("coin", 10));
    // NOTE: zeroerr CHECK_EQ evaluates the LHS twice (compare + print), so
    // side-effecting calls must be stored in a local first.
    int added = bag.addItem("coin", 150);
    CHECK_EQ(added, 150);  // 99 + 51 across two slots
    CHECK_EQ(bag.countItem("coin"), 150);
    CHECK_EQ(bag.getUsedSlotCount(), 2);
    CHECK_EQ(bag.getSlotQuantity(0), 99);
    CHECK_EQ(bag.getSlotQuantity(1), 51);

    int removed = bag.removeItem("coin", 60);
    CHECK_EQ(removed, 60);
    CHECK_EQ(bag.countItem("coin"), 90);
    CHECK_EQ(bag.getSlotQuantity(0), 39);  // removed from first stack first
    CHECK_EQ(bag.getSlotQuantity(1), 51);

    ItemRegistry::clear();
}

TEST_CASE("inventory.bag.acceptTagsAndReject") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();

    ItemDefinition quest;
    quest.id = "quest.letter";
    quest.tags = {"quest"};
    ItemRegistry::registerItem(quest);

    ItemDefinition junk;
    junk.id = "junk";
    junk.tags = {"trash"};
    ItemRegistry::registerItem(junk);

    Bag questBag(4);
    questBag.addAcceptTag("quest");
    CHECK(questBag.canAddItem("quest.letter", 1));
    CHECK(!questBag.canAddItem("junk", 1));
    CHECK_EQ(questBag.canAddItemReason("junk", 1), "accept_tag_mismatch");

    Bag general(4);
    general.addRejectTag("quest");
    CHECK(!general.canAddItem("quest.letter", 1));
    CHECK_EQ(general.canAddItemReason("quest.letter", 1), "rejected_tag");
    CHECK(general.canAddItem("junk", 1));

    ItemRegistry::clear();
}

TEST_CASE("inventory.bag.weightCapacity") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();

    ItemDefinition ore;
    ore.id = "ore";
    ore.maxStack = 10;
    ore.weight = 2.f;
    ItemRegistry::registerItem(ore);

    Bag bag(10);
    bag.setMaxWeight(5.f);
    bag.setCapacityPolicy("slotsAndWeight");

    int added = bag.addItem("ore", 3);
    CHECK_EQ(added, 2);  // 2*2=4 <=5, third would be 6
    CHECK(approxEq(bag.getUsedWeight(), 4.f));
    CHECK(!bag.canAddItem("ore", 1));
    CHECK_EQ(bag.canAddItemReason("ore", 1), "over_weight");

    ItemRegistry::clear();
}

TEST_CASE("inventory.bag.moveSwapSplit") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();

    ItemDefinition gem;
    gem.id = "gem";
    gem.maxStack = 10;
    ItemRegistry::registerItem(gem);

    Bag bag(4);
    bag.addItem("gem", 8);
    CHECK(bag.splitStack(0, 3, 1));
    CHECK_EQ(bag.getSlotQuantity(0), 5);
    CHECK_EQ(bag.getSlotQuantity(1), 3);

    CHECK(bag.moveSlot(1, 2));
    CHECK(bag.isSlotEmpty(1));
    CHECK_EQ(bag.getSlotQuantity(2), 3);

    CHECK(bag.swapSlots(0, 2));
    CHECK_EQ(bag.getSlotQuantity(0), 3);
    CHECK_EQ(bag.getSlotQuantity(2), 5);

    // merge via move
    CHECK(bag.moveSlot(0, 2));
    CHECK(bag.isSlotEmpty(0));
    CHECK_EQ(bag.getSlotQuantity(2), 8);

    ItemRegistry::clear();
}

TEST_CASE("inventory.bag.transferBetweenBags") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();

    ItemDefinition herb;
    herb.id = "herb";
    herb.maxStack = 20;
    herb.weight = 0.1f;
    ItemRegistry::registerItem(herb);

    Bag a(3);
    a.setId("a");
    Bag b(3);
    b.setId("b");
    a.addItem("herb", 15);

    int moved = InventorySystem::transfer(&a, &b, "herb", 10);
    CHECK_EQ(moved, 10);
    CHECK_EQ(a.countItem("herb"), 5);
    CHECK_EQ(b.countItem("herb"), 10);

    ItemRegistry::clear();
}

TEST_CASE("inventory.extension.customAcceptAndStack") {
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();

    ItemDefinition a;
    a.id = "scroll.a";
    a.maxStack = 5;
    a.extra["level"] = "3";
    ItemRegistry::registerItem(a);

    InventorySystem::registerAcceptRule("minLevel3", [](const Bag &, const ItemDefinition &def,
                                                        int, std::string *reason) {
        int level = 0;
        try {
            level = std::stoi(def.getExtra("level", "0"));
        } catch (...) {
        }
        if (level < 3) {
            if (reason) *reason = "level_too_low";
            return false;
        }
        return true;
    });

    InventorySystem::registerStackRule("neverSame",
                                       [](const ItemStack &, const ItemStack &,
                                          const ItemDefinition &) { return false; });

    Bag bag(4);
    bag.setAcceptRule("minLevel3");
    bag.setStackRule("neverSame");
    CHECK(bag.canAddItem("scroll.a", 1));
    int added = bag.addItem("scroll.a", 3);
    CHECK_EQ(added, 3);
    // never stack -> each unit occupies its own slot
    CHECK_EQ(bag.getUsedSlotCount(), 3);

    InventorySystem::unregisterAcceptRule("minLevel3");
    InventorySystem::unregisterStackRule("neverSame");
    ItemRegistry::clear();
}

TEST_CASE("inventory.equipment.equipUnequip") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();

    ItemDefinition sword;
    sword.id = "sword.iron";
    sword.maxStack = 1;
    sword.equipSlot = "weapon";
    sword.tags = {"weapon"};
    ItemRegistry::registerItem(sword);

    ItemDefinition helm;
    helm.id = "helm.iron";
    helm.maxStack = 1;
    helm.equipSlot = "head";
    helm.tags = {"armor"};
    ItemRegistry::registerItem(helm);

    Bag bag(4);
    bag.setId("player");
    bag.addItem("sword.iron", 1);
    bag.addItem("helm.iron", 1);

    EquipmentSet eq;
    eq.setId("eq");
    eq.defineSlot("weapon");
    eq.addSlotAllowedTag("weapon", "weapon");
    eq.defineSlot("head");

    int swordSlot = bag.findItem("sword.iron");
    CHECK(swordSlot >= 0);
    CHECK(eq.equipFromBag("weapon", &bag, swordSlot));
    CHECK_EQ(eq.getSlotItemId("weapon"), "sword.iron");
    CHECK_EQ(bag.countItem("sword.iron"), 0);

    // wrong slot
    int helmSlot = bag.findItem("helm.iron");
    CHECK(!eq.equipFromBag("weapon", &bag, helmSlot));

    CHECK(eq.unequipToBag("weapon", &bag));
    CHECK(eq.isSlotEmpty("weapon"));
    CHECK_EQ(bag.countItem("sword.iron"), 1);

    ItemRegistry::clear();
}

TEST_CASE("inventory.events.andFacade") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();

    Inventory *inv = Inventory::create();
    CHECK(inv != nullptr);
    CHECK_EQ(inv->getName(), "Inventory");

    int n = inv->registerItemsFromJson(
        R"([{"id":"apple","maxStack":10,"weight":0.2,"tags":["food"]}])");
    CHECK_EQ(n, 1);
    CHECK(inv->hasItemDefinition("apple"));
    CHECK(inv->hasAcceptRule("default"));
    CHECK(inv->hasCapacityPolicy("slotsAndWeight"));
    CHECK(inv->hasStackRule("sameItem"));

    Bag *bag = inv->newBag(4);
    bag->setId("pack");
    int added = bag->addItem("apple", 4);
    CHECK_EQ(added, 4);
    CHECK(inv->getChangeEventCount() >= 1);
    bool sawAdd = false;
    for (int i = 0; i < inv->getChangeEventCount(); ++i) {
        if (inv->getChangeEventAction(i) == "add" && inv->getChangeEventItemId(i) == "apple")
            sawAdd = true;
    }
    CHECK(sawAdd);

    inv->clearChangeEvents();
    CHECK_EQ(inv->getChangeEventCount(), 0);

    bag->destroy();
    inv->clearItemDefinitions();
}

TEST_CASE("inventory.bag.uiPreview") {
    ItemRegistry::clear();
    InventorySystem::clearEvents();
    InventorySystem::ensureBuiltins();

    ItemRegistry::loadFromJson(R"([
      {"id":"potion.hp","displayName":"Heal Potion","maxStack":20,"weight":0.2,"tags":["consumable"]},
      {"id":"ore.iron","displayName":"Iron Ore","maxStack":50,"weight":1.0,"tags":["material"]},
      {"id":"sword.iron","displayName":"Iron Sword","maxStack":1,"weight":3.5,
       "equipSlot":"weapon","tags":["weapon"]},
      {"id":"coin","displayName":"Gold Coin","maxStack":99,"weight":0.01,"tags":["currency"]}
    ])");

    Bag bag(8);
    bag.setId("adventurer");
    bag.setMaxWeight(40.f);
    bag.addItem("potion.hp", 7);
    bag.addItem("ore.iron", 23);
    bag.addItem("sword.iron", 1);
    bag.addItem("coin", 120);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);
    eve::window::WindowSettings s;
    s.width = 520;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());

    gfx->setBackgroundColor(Color(0.09f, 0.10f, 0.14f, 1.f));
    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.09f;
    cam->data()->g = 0.10f;
    cam->data()->b = 0.14f;

    // Soft bag silhouette behind the panel.
    auto *panel = Renderable2D::create();
    panel->transform()->x = 40;
    panel->transform()->y = 50;
    panel->sprite()->width = 280;
    panel->sprite()->height = 320;
    panel->sprite()->r = 0.18f;
    panel->sprite()->g = 0.22f;
    panel->sprite()->b = 0.30f;
    panel->sprite()->visible = true;

    int highlighted = 0;
    for (int frame = 0; frame < 90; ++frame) {
        if (frame % 18 == 0) highlighted = (highlighted + 1) % bag.getSlotCount();

        std::vector<WidgetDesc> slots;
        slots.push_back(text("Adventurer Pack", "title"));
        slots.push_back(separator("sep"));
        slots.push_back(text("weight " + std::to_string(int(bag.getUsedWeight())) + " / " +
                                 std::to_string(int(bag.getMaxWeight())),
                             "weight"));
        slots.push_back(progress(bag.getUsedWeight() / bag.getMaxWeight(), "wbar", "load"));
        slots.push_back(separator("sep2"));

        for (int i = 0; i < bag.getSlotCount(); ++i) {
            const std::string id = bag.getSlotItemId(i);
            std::string label = "[" + std::to_string(i) + "] empty";
            if (!id.empty()) {
                const ItemDefinition *def = ItemRegistry::find(id);
                const std::string name = def ? def->displayName : id;
                label = "[" + std::to_string(i) + "] " + name + " x" +
                        std::to_string(bag.getSlotQuantity(i));
                if (i == highlighted) label = "> " + label;
            }
            slots.push_back(text(label, "s" + std::to_string(i)));
        }

        ui->remountAs("bag", window("Inventory", slots, "root"));
        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_EQ(bag.countItem("coin"), 120);
    panel->sprite()->visible = false;
    ItemRegistry::clear();
    win->close();
}
