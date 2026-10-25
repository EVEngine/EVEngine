// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "module-composition",
  "module": "particles"
}
*/
// clang-format on

#include <limits>
#include "particles/ParticleAppearance.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEventJournal.h"
#include "particles/ParticleRecording.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("physics.bake.particleBirthIdentitySurvivesCompactionAndRestart") {
    using namespace eve::particles;
    auto* emitter = ParticleEmitter::createEmitter(4);
    REQUIRE(emitter != nullptr);
    emitter->setRandomSeed(42);
    auto config     = emitter->config();
    auto sim        = emitter->sim();
    config->lifeMin = config->lifeMax = 10;
    config->emissionRate              = 0;
    emitter->emit(2);
    REQUIRE(sim->alive == 2);
    REQUIRE(sim->particles[0].birthSerial == 1);
    REQUIRE(sim->particles[1].birthSerial == 2);
    sim->particles[0].life = 0;
    REQUIRE(advanceEmitterSim(*config, *sim, 0.01f) > 0);
    REQUIRE(sim->alive == 1);
    REQUIRE(sim->particles[0].birthSerial == 2);
    emitter->emit(1);
    REQUIRE(sim->particles[1].birthSerial == 3);
    emitter->stop();
    emitter->start();
    emitter->reset();
    emitter->emit(1);
    REQUIRE(sim->alive == 1);
    REQUIRE(sim->particles[0].birthSerial == 4);
    sim->nextBirthSerial = std::numeric_limits<std::uint64_t>::max();
    emitter->emit(1);
    REQUIRE(sim->particles[1].birthSerial == std::numeric_limits<std::uint64_t>::max());
    REQUIRE(sim->nextBirthSerial == 0);
    const auto randomBefore  = sim->rng;
    const auto droppedBefore = sim->droppedSpawnsThisFrame;
    emitter->emit(1);
    REQUIRE(sim->alive == 2);
    REQUIRE(sim->rng == randomBefore);
    REQUIRE(sim->droppedSpawnsThisFrame == droppedBefore + 1);
    emitter->reset();
    emitter->emit(1);
    REQUIRE(sim->alive == 0);
    ecs::DestroyEntity(emitter);
}

TEST_CASE("physics.bake.particleAppearanceMatchesLiveSpriteModes") {
    using namespace eve::particles;
    ParticleEmitter::Config config;
    config.particleW  = 4;
    config.particleH  = 2;
    config.sizeStart  = 1;
    config.sizeEnd    = 3;
    config.colorStart = eve::Color(1, 0, 0, 1);
    config.colorEnd   = eve::Color(0, 0, 1, 0);
    config.hframes    = 2;
    config.vframes    = 2;
    Particle particle;
    particle.x           = 10;
    particle.y           = 20;
    particle.life        = 1;
    particle.lifetime    = 2;
    particle.size        = 2;
    particle.frame       = -1;
    const auto evaluated = evaluateParticleAppearance(config, particle);
    REQUIRE(static_cast<bool>(evaluated));
    const auto sprite = evaluated.value();
    REQUIRE(sprite.x == 2);
    REQUIRE(sprite.y == 16);
    REQUIRE(sprite.w == 16);
    REQUIRE(sprite.h == 8);
    REQUIRE(sprite.color.r == 0.5f);
    REQUIRE(sprite.color.b == 0.5f);
    REQUIRE(sprite.color.a == 0.5f);
    REQUIRE(sprite.u0 == 0.5f);
    REQUIRE(sprite.v0 == 0.5f);
    REQUIRE(sprite.u1 == 1);
    REQUIRE(sprite.v1 == 1);
    config.renderMode        = "axis";
    config.renderAxisDegrees = 37;
    REQUIRE(evaluateParticleAppearance(config, particle).value().rotation == 37);
    config.renderMode          = "stretched";
    config.stretchFactor       = 3;
    particle.vx                = 0;
    particle.vy                = 10;
    const auto stretchedResult = evaluateParticleAppearance(config, particle);
    REQUIRE(static_cast<bool>(stretchedResult));
    const auto stretched = stretchedResult.value();
    REQUIRE(stretched.w == 30);
    REQUIRE(stretched.rotation == 90);
    REQUIRE(stretched.x == sprite.x);
    REQUIRE(sprite.w == 16);
    particle.frame = std::numeric_limits<float>::max();
    config.hframes = config.vframes = std::numeric_limits<int>::max();
    REQUIRE(static_cast<bool>(evaluateParticleAppearance(config, particle)));
    particle.frame = std::numeric_limits<float>::infinity();
    REQUIRE(!evaluateParticleAppearance(config, particle));
    particle.frame = 0;
    particle.x     = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!evaluateParticleAppearance(config, particle));
}

TEST_CASE("physics.bake.particleJournalRejectsIncompleteRecording") {
    using namespace eve::particles;
    ParticleEventJournal journal;
    REQUIRE(journal.append({0, 1, ParticleRecordEventKind::Snapshot, {}}).ok());
    REQUIRE(journal.events().empty());
    REQUIRE(journal.prepare(2).ok());
    ParticleRecordEvent birth{0, 1, ParticleRecordEventKind::Snapshot, {}};
    REQUIRE(journal.append(birth).ok());
    birth.appearance.x = 99;
    REQUIRE(journal.events()[0].appearance.x == 0);
    REQUIRE(journal.append({1, 1, ParticleRecordEventKind::Death, {}}).ok());
    REQUIRE(!journal.append({2, 2, ParticleRecordEventKind::Snapshot, {}}).ok());
    REQUIRE(journal.state() == ParticleRecordState::Exhausted);
    REQUIRE(!journal.finish().ok());
    REQUIRE(journal.events().size() == 2);
    journal.disable();
    REQUIRE(journal.state() == ParticleRecordState::Exhausted);
    REQUIRE(!journal.prepare(2000001).ok());
    REQUIRE(journal.events().size() == 2);
    REQUIRE(journal.prepare(3).ok());
    REQUIRE(journal.append({1, 1, ParticleRecordEventKind::Snapshot, {}}).ok());
    REQUIRE(!journal.append({0, 1, ParticleRecordEventKind::Death, {}}).ok());
    REQUIRE(journal.state() == ParticleRecordState::InvalidInput);
    REQUIRE(!journal.append({2, 1, ParticleRecordEventKind::Death, {}}).ok());
    REQUIRE(journal.events().size() == 1);
    REQUIRE(!journal.finish().ok());
    REQUIRE(journal.prepare(2).ok());
    REQUIRE(journal.append({0, 2, ParticleRecordEventKind::Snapshot, {}}).ok());
    REQUIRE(journal.append({1, 2, ParticleRecordEventKind::Death, {}}).ok());
    auto owned = journal.finish().expect("finish");
    REQUIRE(journal.state() == ParticleRecordState::Disabled);
    REQUIRE(journal.events().empty());
    REQUIRE(owned.size() == 2);
    REQUIRE(journal.prepare(1).ok());
    REQUIRE(owned[0].birthSerial == 2);
    REQUIRE(!journal.finish().value().size());
}

TEST_CASE("physics.bake.particleSimulationRecordsActualDeathsAndReset") {
    using namespace eve::particles;
    auto* emitter = ParticleEmitter::createEmitter(4);
    emitter->setRandomSeed(7);
    auto cfg     = emitter->config();
    cfg->lifeMin = cfg->lifeMax = 0.05f;
    cfg->emissionRate           = 0;
    REQUIRE(prepareParticleRecording(*emitter, 32).ok());
    emitter->emit(1);
    REQUIRE(emitter->sim()->recording.events().size() == 1);
    REQUIRE(advanceEmitterSim(*cfg, *emitter->sim(), 0.1f) > 0);
    REQUIRE(emitter->sim()->alive == 0);
    const auto death = emitter->sim()->recording.events().back();
    REQUIRE(death.kind == ParticleRecordEventKind::Death);
    REQUIRE(death.seconds > 0);
    REQUIRE(death.birthSerial == 1);
    cfg->lifeMin = cfg->lifeMax = 10;
    emitter->emit(1);
    REQUIRE(!prepareParticleRecording(*emitter, 32).ok());
    REQUIRE(advanceEmitterSim(*cfg, *emitter->sim(), 0.1f) > 0);
    emitter->reset();
    auto events = finishParticleRecording(*emitter).expect("recorded events");
    REQUIRE(events.back().kind == ParticleRecordEventKind::Death);
    REQUIRE(events.back().birthSerial == 2);
    REQUIRE(events.back().seconds > death.seconds);
    ecs::DestroyEntity(emitter);
    REQUIRE(events.front().birthSerial == 1);
}

TEST_CASE("physics.bake.particleRecordingCapturesCollisionAndRejectsPolicyDrift") {
    using namespace eve::particles;
    auto* emitter = ParticleEmitter::createEmitter(4);
    emitter->setRandomSeed(3);
    auto cfg     = emitter->config();
    cfg->lifeMin = cfg->lifeMax = 10;
    cfg->x                      = 100;
    cfg->collisionBoundsEnabled = true;
    cfg->boundsMinX = cfg->boundsMinY = -1;
    cfg->boundsMaxX = cfg->boundsMaxY = 1;
    cfg->collisionRadius              = 0.1f;
    for (const auto mode : {"kill", "bounce"}) {
        cfg->collisionMode         = mode;
        cfg->collisionLifetimeLoss = 1;
        REQUIRE(prepareParticleRecording(*emitter, 16).ok());
        emitter->emit(1);
        REQUIRE(advanceEmitterSim(*cfg, *emitter->sim(), 0.125f) > 0);
        REQUIRE(emitter->sim()->alive == 0);
        auto events = finishParticleRecording(*emitter).expect("collision recording");
        REQUIRE(events.size() == 2);
        REQUIRE(events.back().kind == ParticleRecordEventKind::Death);
        REQUIRE(events.back().seconds == 0.125);
        REQUIRE(events.back().appearance.x < 1);
    }
    cfg->gpuSimulation = true;
    REQUIRE(!prepareParticleRecording(*emitter, 16).ok());
    cfg->gpuSimulation = false;
    cfg->renderMode    = "ribbon";
    REQUIRE(!prepareParticleRecording(*emitter, 16).ok());
    cfg->renderMode = "billboard";
    REQUIRE(prepareParticleRecording(*emitter, 16).ok());
    cfg->sortMode = "distance";
    emitter->emit(1);
    REQUIRE(!finishParticleRecording(*emitter).ok());
    REQUIRE(emitter->sim()->recording.state() == ParticleRecordState::InvalidInput);
    ecs::DestroyEntity(emitter);
}

TEST_CASE("physics.bake.subEmitterEventsKeepSourceBoundaryAcrossUpdateOrder") {
    using namespace eve::particles;
    for (bool targetFirst : {false, true}) {
        auto* source = ParticleEmitter::createEmitter(4);
        auto* target = ParticleEmitter::createEmitter(4);
        source->setRandomSeed(2);
        target->setRandomSeed(3);
        source->config()->lifeMin = source->config()->lifeMax = 0.125f;
        target->config()->lifeMin = target->config()->lifeMax = 10;
        source->config()->subEmitters.push_back({target, "death", 1});
        REQUIRE(prepareParticleRecording(*source, 32).ok());
        REQUIRE(prepareParticleRecording(*target, 32).ok());
        source->emit(1);
        source->sim()->particles[0].vx = 7;
        if (targetFirst) REQUIRE(advanceEmitterSim(*target->config(), *target->sim(), 0.25f) > 0);
        REQUIRE(advanceEmitterSim(*source->config(), *source->sim(), 0.25f) > 0);
        REQUIRE(target->sim()->recordingSeconds == (targetFirst ? 0.25 : 0));
        REQUIRE(target->sim()->recording.events().front().seconds == 0.25);
        REQUIRE(target->sim()->particles[0].vx == 7);
        if (!targetFirst) REQUIRE(advanceEmitterSim(*target->config(), *target->sim(), 0.25f) > 0);
        REQUIRE(advanceEmitterSim(*target->config(), *target->sim(), 0.25f) > 0);
        auto events = finishParticleRecording(*target).expect("target events");
        REQUIRE(events.front().seconds == 0.25);
        REQUIRE(events.back().seconds == 0.5);
        REQUIRE(events.back().kind == ParticleRecordEventKind::Death);
        REQUIRE(finishParticleRecording(*source).ok());
        source->config()->subEmitters.clear();
        ecs::DestroyEntity(target);
        ecs::DestroyEntity(source);
    }
    auto* source = ParticleEmitter::createEmitter(4);
    auto* target = ParticleEmitter::createEmitter(4);
    source->setRandomSeed(2);
    target->setRandomSeed(3);
    source->config()->lifeMin = source->config()->lifeMax = 0.125f;
    source->config()->subEmitters.push_back({target, "death", 0});
    REQUIRE(prepareParticleRecording(*source, 32).ok());
    REQUIRE(prepareParticleRecording(*target, 32).ok());
    source->emit(1);
    REQUIRE(advanceEmitterSim(*target->config(), *target->sim(), 0.5f) > 0);
    REQUIRE(advanceEmitterSim(*source->config(), *source->sim(), 0.25f) > 0);
    REQUIRE(target->sim()->recording.state() == ParticleRecordState::InvalidInput);
    REQUIRE(target->sim()->recordingSeconds == 0.5);
    REQUIRE(!finishParticleRecording(*target).ok());
    REQUIRE(finishParticleRecording(*source).ok());
    source->config()->subEmitters.clear();
    ecs::DestroyEntity(target);
    ecs::DestroyEntity(source);
}
