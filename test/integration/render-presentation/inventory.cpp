#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Inventory.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::inventory;
using namespace eve::graphics;
using namespace eve::ui;

namespace {
[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }
}  // namespace


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

        REQUIRE(UIHost::resolve(ui->remountAs("bag", window("Inventory", slots, "root"))).has_value());
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
