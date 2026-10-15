#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "filesystem/Filesystem.h"
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
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "particles/UnderwaterParticles.h"
#include "particles/GroundParticleCulling.h"
#include "particles/FloatingPointFixParticles.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <string>

using namespace eve::particles;

namespace {

[[maybe_unused]] void hideAllEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

}  // namespace


TEST_CASE("particles.groundCulling.filtersVisitorAndControlsPlayback") {
    constexpr int zoneTag = 7201;
    constexpr int playerTag = 7202;
    auto* mod = Particles::create();
    auto* emitter = mod->newEmitter(32);
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(
        physics->newWorld3D(0.0F, 0.0F, 0.0F, false));
    auto* zone = world->newBody("static", 0.0F, 0.0F, 0.0F);
    auto* sensor = zone->newSphereShape(5.0F);
    sensor->setTag(zoneTag);
    sensor->setSensor(true);
    auto* player = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    player->newSphereShape(0.5F)->setTag(playerTag);
    REQUIRE(emitter->isStopped());
    REQUIRE(static_cast<bool>(applyGroundParticleCulling(emitter, playerTag, 99, true, false)));
    CHECK(emitter->isStopped());
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    REQUIRE_EQ(world->getBeginTriggerSensorShapeTag(0), zoneTag);
    REQUIRE(static_cast<bool>(applyGroundParticleCulling(
        emitter, playerTag, world->getBeginTriggerVisitorShapeTag(0), true, false)));
    CHECK(emitter->isActive());
    CHECK(!applyGroundParticleCulling(emitter, playerTag, playerTag, true, true));
    CHECK(emitter->isActive());
    player->setPosition(10.0F, 0.0F, 0.0F);
    world->update(1.0F / 60.0F);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    REQUIRE(static_cast<bool>(applyGroundParticleCulling(
        emitter, playerTag, world->getEndTriggerVisitorShapeTag(0), false, true)));
    CHECK(emitter->isStopped());
    CHECK(!applyGroundParticleCulling(nullptr, playerTag, playerTag, true, false));
}


TEST_CASE("particles.config.loadFileAndHotReload") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_particles_cfg", true));
    REQUIRE(fs->setupWriteDirectory());

    const char *name = "fx_spark.json";
    const char *json1 = R"({"emissionRate":11,"x":1,"y":2,"particleLifetime":[1,1]})";
    fs->write(name, json1, std::strlen(json1));

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    CHECK(e->loadConfig(name));
    CHECK_EQ(e->getConfigPath(), std::string(name));
    CHECK(std::abs(e->getEmissionRate() - 11.f) < 1e-4f);
    CHECK(e->getAutoReload());

    // Force stale modtime so poll reloads without waiting on FS resolution.
    e->resource()->modtime = 0;
    const char *json2 = R"({"emissionRate":77,"x":9,"y":8,"particleLifetime":[2,2]})";
    fs->write(name, json2, std::strlen(json2));

    CHECK_EQ(ParticleConfigSystem::poll(), 1);
    CHECK(std::abs(e->getEmissionRate() - 77.f) < 1e-4f);
    CHECK(std::abs(e->getX() - 9.f) < 1e-4f);

    e->setAutoReload(false);
    e->resource()->modtime = 0;
    const char *json3 = R"({"emissionRate":1})";
    fs->write(name, json3, std::strlen(json3));
    CHECK_EQ(ParticleConfigSystem::poll(), 0);
    CHECK(std::abs(e->getEmissionRate() - 77.f) < 1e-4f);

    CHECK(e->reloadConfig());
    CHECK(std::abs(e->getEmissionRate() - 1.f) < 1e-4f);
}

TEST_CASE("particles.config.newEmitterFromFile") {
    auto *fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_particles_fromfile", true));
    REQUIRE(fs->setupWriteDirectory());

    const char *name = "fx_fromfile.json";
    const char *json = R"({"buffer":48,"preset":"smoke","emissionRate":25,"autoStart":true})";
    fs->write(name, json, std::strlen(json));

    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitterFromFile(name);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->getBufferSize(), 48);
    CHECK(std::abs(e->getEmissionRate() - 25.f) < 1e-4f);
    CHECK(e->isActive());
    CHECK_EQ(e->getConfigPath(), std::string(name));
}

TEST_CASE("particles.render.fireAndSmokePreview") {
    hideAllEmitters();

    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 480;
    s.height = 360;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Particles::create();
    ParticleEmitter *fire = mod->newEmitter(256);
    fire->applyPreset("fire");
    fire->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.72f);
    fire->setVisible(true);
    fire->start();

    ParticleEmitter *smoke = mod->newEmitter(128);
    smoke->applyPreset("smoke");
    smoke->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.62f);
    smoke->setVisible(true);
    smoke->start();

    ParticleEmitter *spark = mod->newEmitter(96);
    spark->applyPreset("spark");
    spark->setPosition(float(gfx->getWidth()) * 0.5f, float(gfx->getHeight()) * 0.55f);
    spark->setVisible(true);
    spark->start();

    // Seed a few frames so the first present already has particles.
    for (int i = 0; i < 8; ++i) ParticleSimSystem::update(0.016f);

    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    int framesAlive = 0;
    for (int frame = 0; frame < 90; ++frame) {
        ParticleSimSystem::update(0.016f);
        gfx->clearScreen();
        ParticleRenderSystem::render(gfx);
        gfx->present();

        if (fire->getCount() > 0) ++framesAlive;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(framesAlive, 30);
    CHECK_GT(fire->getCount(), 0);

    fire->setVisible(false);
    smoke->setVisible(false);
    spark->setVisible(false);
    win->close();
}
