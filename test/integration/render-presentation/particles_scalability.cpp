#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

[[maybe_unused]] void resetScalability() {
    auto& budget                = particleBudgetConfig();
    budget.maxParticles         = 0;
    budget.maxSimulatedEmitters = 0;
    budget.qualityLevel         = 3;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    std::vector<ParticleEmitter*> existing;
    auto                          view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg] = *it;
        if (cfg->entity) existing.push_back(cfg->entity);
    }
    for (auto* emitter : existing) {
        emitter->stop();
        emitter->reset();
    }
}

[[maybe_unused]] ParticleEmitter* makeBudgetEmitter(int priority, float rate = 100.f) {
    auto* emitter = Particles::create()->newEmitter(256);
    emitter->setPriority(priority);
    emitter->setRandomSeed(1000 + priority);
    emitter->setEmissionRate(rate);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->start();
    return emitter;
}

}  // namespace


TEST_CASE("particles.scalability.renderBudgetStressScene") {
    resetScalability();
    particleBudgetConfig().maxParticles         = 900;
    particleBudgetConfig().maxSimulatedEmitters = 12;

    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 720;
    settings.height   = 480;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto*         whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    for (int i = 0; i < 24; ++i) {
        auto* emitter = Particles::create()->newEmitter(180);
        emitter->setTexture(whiteTexture);
        emitter->setPriority(24 - i);
        emitter->setRandomSeed(9000 + i);
        emitter->setPosition(74.f + float(i % 6) * 114.f, 82.f + float(i / 6) * 104.f);
        emitter->setEmissionRate(72.f);
        emitter->setMaxSpawnPerFrame(4);
        emitter->setParticleLifetime(0.8f, 1.45f);
        emitter->setParticleSize(13.f, 13.f);
        emitter->setSizes(1.f, 0.25f);
        emitter->setSpeed(18.f, 62.f);
        emitter->setSpread(6.2831853f);
        emitter->setGravity(0.f, 22.f);
        emitter->setDamping(0.18f);
        const float hue = float(i % 6) / 6.f;
        emitter->setColorStart(0.25f + hue * 0.7f, 0.85f - hue * 0.35f, 1.f - hue * 0.5f, 0.95f);
        emitter->setColorEnd(0.15f, 0.05f, 0.35f + hue * 0.5f, 0.1f);
        emitter->start();
    }

    gfx->setBackgroundColorRGBA(0.018f, 0.025f, 0.055f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 120; ++frame) {
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(22.f, 20.f, 676.f, 440.f, 0.035f, 0.055f, 0.11f, 1.f);
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    const auto& stats = particleFrameStats();
    REQUIRE_EQ(stats.emittersTotal, 24);
    REQUIRE_EQ(stats.emittersSimulated, 12);
    REQUIRE_EQ(stats.emittersBudgetSkipped, 12);
    REQUIRE(stats.particlesAfter <= 900);
    REQUIRE_GT(stats.renderedParticles, 100);
    REQUIRE(stats.simulationMs >= 0.0);
    REQUIRE(stats.renderMs >= 0.0);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_scalability_stress.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > uintmax_t(1024));
    window->close();
}
