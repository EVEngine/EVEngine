#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <string>

using namespace eve::particles;

namespace {

void hideAllEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw] = *it;
        draw->visible = false;
    }
}

}  // namespace

TEST_CASE("particles.newEmitter.ecsView") {
    auto *mod = Particles::create();
    int before = mod->getEmitterCount();
    ParticleEmitter *e = mod->newEmitter(64);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->getBufferSize(), 64);
    CHECK_EQ(e->getCount(), 0);
    CHECK_EQ(mod->getEmitterCount(), before + 1);
    CHECK(e->config()->entity == e);
}

TEST_CASE("particles.emitter.emitAndLifetime") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->emit(10);
    CHECK_EQ(e->getCount(), 10);

    ParticleSimSystem::update(0.5f);
    CHECK_EQ(e->getCount(), 10);

    ParticleSimSystem::update(0.6f);
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.emissionRate") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(100);
    e->setParticleLifetime(10.f, 10.f);
    e->setEmissionRate(50.f);
    e->setSpeed(0.f, 0.f);
    e->start();
    ParticleSimSystem::update(0.2f); // ~10 particles
    CHECK_GE(e->getCount(), 9);
    CHECK_LE(e->getCount(), 11);
}

TEST_CASE("particles.emitter.startStopPause") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setEmissionRate(100.f);
    e->setParticleLifetime(5.f, 5.f);
    CHECK(e->isStopped());

    e->start();
    CHECK(e->isActive());
    e->pause();
    CHECK(e->isPaused());
    CHECK(!e->isActive());
    int c0 = e->getCount();
    ParticleSimSystem::update(1.f);
    CHECK_EQ(e->getCount(), c0);

    e->start();
    e->stop();
    CHECK(e->isStopped());
}

TEST_CASE("particles.emitter.motion") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setPosition(0.f, 0.f);
    e->setParticleLifetime(2.f, 2.f);
    e->setDirection(0.f);
    e->setSpread(0.f);
    e->setSpeed(100.f, 100.f);
    e->setLinearAcceleration(0.f, 0.f, 0.f, 0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), 1);
}

TEST_CASE("particles.emitter.applyPreset") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->applyPreset("spark");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("smoke");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("fire");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("unknown");
    CHECK_GT(e->getEmissionRate(), 0.f);
}

TEST_CASE("particles.system.updateAll") {
    auto *mod = Particles::create();
    ParticleEmitter *a = mod->newEmitter(16);
    ParticleEmitter *b = mod->newEmitter(16);
    a->setParticleLifetime(1.f, 1.f);
    b->setParticleLifetime(1.f, 1.f);
    a->emit(3);
    b->emit(5);
    ParticleSimSystem::update(2.f);
    CHECK_EQ(a->getCount(), 0);
    CHECK_EQ(b->getCount(), 0);
}

TEST_CASE("particles.module.updateForwardsToSystem") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(1.f, 1.f);
    e->emit(4);
    mod->update(2.f);
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.reset") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->emit(8);
    CHECK_EQ(e->getCount(), 8);
    e->reset();
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.bufferCap") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(4);
    e->setParticleLifetime(10.f, 10.f);
    e->emit(100);
    CHECK_EQ(e->getCount(), 4);
}

TEST_CASE("particles.renderSystem.skipsInvisible") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->emit(3);
    e->setVisible(false);
    // No Graphics — render should early-out safely.
    ParticleRenderSystem::render(nullptr);
    CHECK_EQ(e->getCount(), 3);
}

TEST_CASE("particles.emitter.emissionAreaAndForces") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setPosition(100.f, 100.f);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("ellipse", 40.f, 20.f);
    e->setRadialAcceleration(50.f, 50.f);
    e->setTangentialAcceleration(0.f, 0.f);
    e->setSizeVariation(0.5f);
    e->setSpin(1.f, 2.f);
    CHECK_EQ(e->getEmissionAreaType(), std::string("ellipse"));
    e->emit(20);
    CHECK_EQ(e->getCount(), 20);

    // Spawn offsets should leave the point origin for ellipse area.
    bool anyOffset = false;
    auto sim = e->sim();
    for (int i = 0; i < sim->alive; ++i) {
        const auto &p = sim->particles[size_t(i)];
        if (std::abs(p.x - 100.f) > 0.5f || std::abs(p.y - 100.f) > 0.5f) anyOffset = true;
        CHECK_GT(p.size, 0.f);
        CHECK_GE(p.spin, 1.f);
        CHECK_LE(p.spin, 2.f);
    }
    CHECK(anyOffset);

    ParticleSimSystem::update(0.2f);
    CHECK_EQ(e->getCount(), 20);
}

TEST_CASE("particles.config.applyAreaAndAccel") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    const char *json = R"({
      "emissionArea": {"type":"rect","x":8,"y":4},
      "radialAcceleration": [10, 20],
      "tangentialAcceleration": [-5, 5],
      "sizeVariation": 0.2,
      "spin": [1, 3]
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->getEmissionAreaType(), std::string("rect"));
    CHECK(std::abs(e->getEmissionAreaX() - 8.f) < 1e-4f);
    CHECK(std::abs(e->getEmissionAreaY() - 4.f) < 1e-4f);
    CHECK(std::abs(e->getSizeVariation() - 0.2f) < 1e-4f);
}

TEST_CASE("particles.renderSystem.cameraTransform") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setPosition(0.f, 0.f);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->emit(1);
    auto *cam = eve::graphics::Camera2D::createCamera();
    cam->data()->x = 0.f;
    cam->data()->y = 0.f;
    cam->data()->zoom = 2.f;
    e->setCamera(cam);
    // No Graphics — must not crash; camera path is covered by wiring.
    ParticleRenderSystem::render(nullptr);
    CHECK_EQ(e->getCount(), 1);
}

TEST_CASE("particles.config.applyJsonText") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    const char *json = R"({
      "preset": "spark",
      "x": 12.5,
      "y": 34,
      "emissionRate": 40,
      "particleLifetime": [0.5, 1.5],
      "speed": [10, 20],
      "colorStart": [1, 0, 0, 1],
      "colorEnd": [0, 0, 1, 0],
      "layer": 3,
      "autoStart": true
    })";
    CHECK(e->applyConfig(json));
    CHECK(std::abs(e->getX() - 12.5f) < 1e-4f);
    CHECK(std::abs(e->getY() - 34.f) < 1e-4f);
    CHECK(std::abs(e->getEmissionRate() - 40.f) < 1e-4f);
    CHECK(std::abs(e->getParticleLifetimeMin() - 0.5f) < 1e-4f);
    CHECK(std::abs(e->getParticleLifetimeMax() - 1.5f) < 1e-4f);
    CHECK_EQ(e->getLayer(), 3);
    CHECK(e->isActive());
}

TEST_CASE("particles.config.applyInvalid") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    CHECK(!e->applyConfig("{bad"));
    CHECK(!e->applyConfig("[1,2,3]"));
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
    win->setGraphics(gfx);
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
