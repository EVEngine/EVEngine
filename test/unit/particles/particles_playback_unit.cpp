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

TEST_CASE("particles.playback.seedIsReproducible") {
    auto* particles = Particles::create();
    auto* a         = particles->newEmitter(16);
    auto* b         = particles->newEmitter(16);
    configureDeterministicEmitter(a, 1337);
    configureDeterministicEmitter(b, 1337);

    a->emit(8);
    b->emit(8);
    REQUIRE(a->getCount() == b->getCount());
    for (int i = 0; i < a->getCount(); ++i) {
        const Particle& pa = a->sim()->particles[size_t(i)];
        const Particle& pb = b->sim()->particles[size_t(i)];
        CHECK(std::abs(pa.life - pb.life) < 1e-6f);
        CHECK(std::abs(pa.vx - pb.vx) < 1e-6f);
        CHECK(std::abs(pa.vy - pb.vy) < 1e-6f);
        CHECK(std::abs(pa.size - pb.size) < 1e-6f);
        CHECK(std::abs(pa.rot - pb.rot) < 1e-6f);
    }
}

TEST_CASE("particles.playback.startRewindsFixedSeed") {
    auto* emitter = Particles::create()->newEmitter(8);
    configureDeterministicEmitter(emitter, 42);
    emitter->start();
    emitter->emit(1);
    const Particle first = emitter->sim()->particles[0];

    emitter->reset();
    emitter->start();
    emitter->emit(1);
    const Particle replay = emitter->sim()->particles[0];
    CHECK(std::abs(first.life - replay.life) < 1e-6f);
    CHECK(std::abs(first.vx - replay.vx) < 1e-6f);
    CHECK(std::abs(first.vy - replay.vy) < 1e-6f);
}

TEST_CASE("particles.playback.fixedStepAndSpeed") {
    auto* emitter = Particles::create()->newEmitter(4);
    emitter->setRandomSeed(7);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setDirection(0.f);
    emitter->setSpread(0.f);
    emitter->setSpeed(10.f, 10.f);
    emitter->setFixedTimeStep(0.1f, 4);
    emitter->setPlaybackSpeed(2.f);
    emitter->emit(1);

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
    CHECK(std::abs(emitter->sim()->particles[0].x) < 1e-6f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
    CHECK(std::abs(emitter->sim()->particles[0].x - 1.f) < 1e-5f);
}

TEST_CASE("particles.playback.loopResetsTimelineAndBursts") {
    auto* emitter = Particles::create()->newEmitter(16);
    emitter->setRandomSeed(9);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setEmitterLifetime(0.1f);
    emitter->setLooping(true);
    emitter->addBurst(0.f, 2);
    emitter->start();

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.11f);
    CHECK(emitter->isActive());
    CHECK_EQ(emitter->getCount(), 2);
    CHECK(emitter->sim()->emitterAge < 0.1f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
    CHECK_EQ(emitter->getCount(), 4);
}

TEST_CASE("particles.playback.distanceEmissionIsEvenlySpaced") {
    auto* emitter = Particles::create()->newEmitter(16);
    emitter->setRandomSeed(10);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setEmissionRateOverDistance(0.5f);
    emitter->setPosition(0.f, 0.f);
    emitter->start();
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);

    emitter->setPosition(10.f, 0.f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
    REQUIRE(emitter->getCount() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(std::abs(emitter->sim()->particles[size_t(i)].x - float((i + 1) * 2)) < 1e-5f);
        CHECK(std::abs(emitter->sim()->particles[size_t(i)].y) < 1e-6f);
    }
}

TEST_CASE("particles.playback.configAppliesProfessionalControls") {
    auto*       emitter = Particles::create()->newEmitter(8);
    const char* json    = R"({
        "emissionRateOverDistance": 3.5,
        "emitterLife": 2.0,
        "looping": true,
        "playbackSpeed": 0.5,
        "fixedTimeStep": 0.0166667,
        "maxSubSteps": 6,
        "randomSeed": 1234,
        "autoRandomSeed": false
    })";
    REQUIRE(emitter->applyConfig(json));
    CHECK(std::abs(emitter->getEmissionRateOverDistance() - 3.5f) < 1e-6f);
    CHECK(emitter->getLooping());
    CHECK(std::abs(emitter->getPlaybackSpeed() - 0.5f) < 1e-6f);
    CHECK(std::abs(emitter->getFixedTimeStep() - 0.0166667f) < 1e-6f);
    CHECK_EQ(emitter->config()->maxSubSteps, 6);
    CHECK_EQ(emitter->getRandomSeed(), 1234);
    CHECK(!emitter->getAutoRandomSeed());
}

TEST_CASE("particles.playback.exposedParametersDriveRuntimeScales") {
    auto* emitter = Particles::create()->newEmitter(32);
    emitter->setRandomSeed(91);
    emitter->setEmissionRate(10.f);
    emitter->setParticleLifetime(5.f, 5.f);
    emitter->setSpeed(20.f, 20.f);
    emitter->setSizeVariation(0.f);
    emitter->setFloatParameter("intensity", 2.f);
    emitter->bindFloatParameter("intensity", "emission");
    emitter->bindFloatParameter("intensity", "speed", 0.5f, 0.f);
    emitter->bindFloatParameter("intensity", "size", 0.75f, 0.f);
    emitter->start();

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.1f);
    REQUIRE_EQ(emitter->getCount(), 2);
    CHECK(std::abs(emitter->sim()->particles[0].size - 1.5f) < 1e-5f);
    const auto& particle = emitter->sim()->particles[0];
    CHECK(std::abs(std::sqrt(particle.vx * particle.vx + particle.vy * particle.vy) - 20.f) < 1e-4f);

    emitter->bindFloatParameter("freeze", "playback");
    emitter->setFloatParameter("freeze", 0.f);
    const float life = emitter->sim()->particles[0].life;
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 1.f);
    CHECK(std::abs(emitter->sim()->particles[0].life - life) < 1e-5f);
}
