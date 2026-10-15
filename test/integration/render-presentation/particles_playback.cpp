#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <cmath>
#include <cstdint>
#include <filesystem>

using namespace eve::particles;

namespace {

[[maybe_unused]] void configureDeterministicEmitter(ParticleEmitter* emitter, int seed) {
    emitter->setRandomSeed(seed);
    emitter->setPosition(12.f, 34.f);
    emitter->setParticleLifetime(0.5f, 2.f);
    emitter->setDirection(0.7f);
    emitter->setSpread(1.2f);
    emitter->setSpeed(10.f, 80.f);
    emitter->setSizeVariation(0.5f);
    emitter->setStartRotation(-90.f, 90.f);
}

}  // namespace


TEST_CASE("particles.playback.renderDistanceTrailAndLoopingBurst") {
    auto* window = eve::window::Window::create();
    auto* gfx    = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 420;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);

    auto*         particles     = Particles::create();
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    auto*         whiteTexture  = gfx->newTexture(1, 1, whitePixel);
    auto*         trail         = particles->newEmitter(900);
    trail->setTexture(whiteTexture);
    trail->setRandomSeed(20260826);
    trail->setEmissionRateOverDistance(0.38f);
    trail->setParticleLifetime(0.65f, 1.15f);
    trail->setParticleSize(22.f, 22.f);
    trail->setSizes(1.f, 0.45f);
    trail->setSizeVariation(0.35f);
    trail->setSpeed(0.f, 0.f);
    trail->setSpread(6.2831853f);
    trail->setGravity(0.f, 0.f);
    trail->setDamping(0.25f);
    trail->setBlendMode("alpha");
    trail->setColorStart(0.35f, 0.9f, 1.f, 1.f);
    trail->setColorEnd(0.45f, 0.15f, 1.f, 0.35f);
    trail->setRibbon(0.55f, 2.f);
    trail->setFixedTimeStep(1.f / 120.f, 8);
    trail->setPosition(320.f, 210.f);
    trail->start();

    auto* burst = particles->newEmitter(600);
    burst->setTexture(whiteTexture);
    burst->setRandomSeed(424242);
    burst->setPosition(320.f, 210.f);
    burst->setEmitterLifetime(0.9f);
    burst->setLooping(true);
    burst->addBurst(0.f, 72);
    burst->setParticleLifetime(0.7f, 1.4f);
    burst->setParticleSize(18.f, 18.f);
    burst->setSizes(1.2f, 0.4f);
    burst->setSpeed(80.f, 210.f);
    burst->setSpread(6.2831853f);
    burst->setGravity(0.f, 95.f);
    burst->setDamping(0.18f);
    burst->setBlendMode("alpha");
    burst->clearColorGradient();
    burst->addColorStop(0.f, 1.f, 0.96f, 0.65f, 1.f);
    burst->addColorStop(0.35f, 1.f, 0.35f, 0.08f, 0.9f);
    burst->addColorStop(1.f, 0.3f, 0.04f, 0.01f, 0.3f);
    burst->setFixedTimeStep(1.f / 120.f, 8);
    burst->start();

    gfx->setBackgroundColorRGBA(0.025f, 0.035f, 0.065f, 1.f);
    constexpr float dt = 1.f / 60.f;
    for (int frame = 0; frame < 120; ++frame) {
        const float t = float(frame) * dt;
        trail->setPosition(320.f + std::cos(t * 1.35f) * 240.f, 210.f + std::sin(t * 2.05f) * 125.f);
        ParticleSimSystem::update(dt);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(28.f, 26.f, 584.f, 368.f, 0.04f, 0.065f, 0.12f, 1.f);
        ParticleRenderSystem::render(gfx);
        gfx->present();
    }

    REQUIRE_GT(trail->getCount(), 20);
    REQUIRE_GT(burst->getCount(), 20);
    int particlesInView = 0;
    for (auto* emitter : {trail, burst}) {
        for (int i = 0; i < emitter->getCount(); ++i) {
            const Particle& p = emitter->sim()->particles[size_t(i)];
            if (p.x >= 0.f && p.x < 640.f && p.y >= 0.f && p.y < 420.f) ++particlesInView;
        }
    }
    REQUIRE_GT(particlesInView, 20);

    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/particle_playback_lab.png";
    CHECK(gfx->saveFramePng(output));
    CHECK(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > uintmax_t(1024));
    window->close();
}
