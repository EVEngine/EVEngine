#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionInstance.h"
#include "physics/destruction/editing/FractureRecipeSchema.h"
#include "physics/destruction/graphics/GeometryCollectionRenderer.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <memory>

using eve::physics::BoneRuntimeState;
using eve::physics::DestructionField;
using eve::physics::DestructionFieldFalloff;
using eve::physics::DestructionFieldKind;
using eve::physics::GeometryCollectionAsset;
using eve::physics::GeometryCollectionInstance;
using eve::physics::GeometryCollectionRenderer;
using eve::physics::Physics;
using eve::physics::World3D;

namespace {

eve::SimulationStep simStep(std::uint64_t tick) {
    return {eve::SimulationTick{tick}, eve::Duration::fromSeconds(1.0 / 60.0).expect("destruction gfx dt")};
}

bool tryInitDrawWindow(eve::window::Window*& window, eve::graphics::Graphics*& graphics) {
    window = eve::window::Window::create();
    graphics = eve::graphics::Graphics::create();
    if (!window || !graphics) return false;
    eve::window::WindowSettings settings;
    settings.width = 320;
    settings.height = 240;
    settings.centered = true;
    return window->setWindowSettings(settings);
}

}  // namespace

TEST_CASE("physics_destruction_editing.fractureRecipeSchemaMirrorsDefaults") {
    auto schema = eve::physics_editing::fractureRecipeSchema();
    REQUIRE_EQ(schema.typeId, std::string(eve::physics::FractureRecipe::SchemaId));
    REQUIRE_EQ(schema.version, eve::physics::FractureRecipe::SchemaVersion);
    REQUIRE(schema.find(eve::editing::PropertyPath("mode")));
    REQUIRE(schema.find(eve::editing::PropertyPath("seed")));
    REQUIRE(schema.find(eve::editing::PropertyPath("siteCountMin")));
    const auto mode = schema.find(eve::editing::PropertyPath("mode"));
    REQUIRE_EQ(mode->enumItems.size(), 4u);
}

TEST_CASE("physics_destruction_graphics.drawsActiveAndBatchesSleepingBones") {
    eve::window::Window* window = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    if (!tryInitDrawWindow(window, gfx)) {
        // Headless hosts without a display/Vulkan ICD skip the draw path.
        return;
    }

    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(0.5f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());

    GeometryCollectionRenderer renderer(instance.get());
    renderer.setExteriorColor(0.6f, 0.6f, 0.6f, 1.f);
    renderer.setInteriorColor(0.9f, 0.3f, 0.2f, 1.f);

    gfx->begin3DFrame();
    auto drawn = renderer.draw(gfx);
    REQUIRE(drawn.ok());
    REQUIRE_EQ(renderer.lastActiveDrawCount(), 2);
    REQUIRE_EQ(renderer.lastSleepBatchCount(), 0);
    gfx->present();

    DestructionField strain;
    strain.kind = DestructionFieldKind::Strain;
    strain.falloff = DestructionFieldFalloff::Linear;
    strain.centerX = 0.f;
    strain.centerY = 1.f;
    strain.centerZ = 0.f;
    strain.radius = 3.f;
    strain.magnitude = 2.f;
    REQUIRE(instance->applyField(strain).ok());
    REQUIRE(instance->step(simStep(1)).ok());
    REQUIRE(instance->boneState(0) == BoneRuntimeState::Detached);
    REQUIRE(instance->boneState(1) == BoneRuntimeState::Detached);

    auto body0 = instance->boneLink(0).value().resolve(*world);
    auto body1 = instance->boneLink(1).value().resolve(*world);
    REQUIRE(body0.ok());
    REQUIRE(body1.ok());
    body0.value()->setLinearVelocity(0.f, 0.f, 0.f);
    body1.value()->setLinearVelocity(0.f, 0.f, 0.f);

    DestructionField sleep;
    sleep.kind = DestructionFieldKind::Sleep;
    sleep.falloff = DestructionFieldFalloff::None;
    sleep.centerX = 0.f;
    sleep.centerY = 1.f;
    sleep.centerZ = 0.f;
    sleep.radius = 5.f;
    REQUIRE(instance->applyField(sleep).ok());
    REQUIRE(instance->boneState(0) == BoneRuntimeState::Sleeping);
    REQUIRE(instance->boneState(1) == BoneRuntimeState::Sleeping);
    REQUIRE(instance->sleepBatchRevision() > 0u);

    gfx->begin3DFrame();
    auto drawnSleep = renderer.draw(gfx);
    REQUIRE(drawnSleep.ok());
    REQUIRE_EQ(renderer.lastActiveDrawCount(), 0);
    REQUIRE_EQ(renderer.lastSleepBatchCount(), 2);
    gfx->present();
    window->close();
}

TEST_CASE("physics_destruction_graphics.staleWorldRejectsDraw") {
    auto* mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f);
    REQUIRE(asset.ok());
    auto created = GeometryCollectionInstance::create(*world, asset.value(), 0.f, 0.f, 0.f);
    REQUIRE(created.ok());
    auto instance = std::move(created.value());
    GeometryCollectionRenderer renderer(instance.get());
    world->destroy();
    world.reset();
    REQUIRE(!instance->hasLiveWorld());
    auto drawn = renderer.draw(nullptr);
    REQUIRE(!drawn.ok());
}
