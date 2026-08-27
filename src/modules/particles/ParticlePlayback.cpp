#include "particles/ParticleEmitter.h"

#include "common/Module.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace eve::particles {

float advanceEmitterSim(ParticleEmitter::Config& cfg, ParticleEmitter::Sim& sim, float dt) {
    const float playbackScale = cfg.resolvedParameterScale("playback");
    if (sim.paused || dt <= 0.f || cfg.playbackSpeed <= 0.f || playbackScale <= 0.f) return 0.f;

    float scaledDt = dt * cfg.playbackSpeed * playbackScale;
    if (cfg.maxDeltaTime > 0.f && scaledDt > cfg.maxDeltaTime) scaledDt = cfg.maxDeltaTime;

    if (cfg.fixedTimeStep <= 0.f) {
        stepEmitterSim(cfg, sim, scaledDt);
        return scaledDt;
    }

    sim.fixedTimeAccum += scaledDt;
    const float step     = cfg.maxDeltaTime > 0.f ? std::min(cfg.fixedTimeStep, cfg.maxDeltaTime) : cfg.fixedTimeStep;
    const int   maxSteps = cfg.maxSubSteps > 0 ? cfg.maxSubSteps : 1;
    int         steps    = 0;
    while (sim.fixedTimeAccum + 1e-7f >= step && steps < maxSteps) {
        stepEmitterSim(cfg, sim, step);
        sim.fixedTimeAccum -= step;
        ++steps;
    }

    // Bound retained debt as well as work per frame. A suspended process must
    // not spend many later frames catching up and producing a VFX burst.
    if (steps == maxSteps && sim.fixedTimeAccum >= step) sim.fixedTimeAccum = std::fmod(sim.fixedTimeAccum, step);
    return float(steps) * step;
}

eve::Result<void> advanceEmitterSim(ParticleEmitter::Config& cfg,
                                    ParticleEmitter::Sim& sim,
                                    const eve::SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "particle simulation delta must be non-negative"));
    if (sim.hasSimulationTick && step.tick <= sim.simulationTick)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "particle simulation tick must advance monotonically"));

    const float dt = static_cast<float>(step.delta.seconds());
    if (!std::isfinite(dt))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "particle simulation delta is outside float range"));

    // Scheduler-owned pause, rate and fixed-step policy are already reflected
    // in the supplied SimulationStep. The legacy emitter playback controls are
    // intentionally not consulted here.
    if (!sim.paused && dt > 0.f) stepEmitterSim(cfg, sim, dt);
    sim.simulationTick = step.tick;
    sim.hasSimulationTick = true;
    return eve::Result<void>::success(eve::Status::success(
        (sim.paused || dt == 0.f) ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

eve::Result<void> ParticleEmitter::advance(const eve::SimulationStep& step) {
    return advanceEmitterSim(*config(), *sim(), step);
}

void ParticleEmitter::setEmissionRateOverDistance(float rate) {
    config()->emissionRateOverDistance = rate > 0.f ? rate : 0.f;
}

float ParticleEmitter::getEmissionRateOverDistance() { return config()->emissionRateOverDistance; }

void ParticleEmitter::setLooping(bool looping) { config()->looping = looping; }
bool ParticleEmitter::getLooping() { return config()->looping; }

void ParticleEmitter::setPlaybackSpeed(float speed) { config()->playbackSpeed = speed > 0.f ? speed : 0.f; }

float ParticleEmitter::getPlaybackSpeed() { return config()->playbackSpeed; }

void ParticleEmitter::setFixedTimeStep(float seconds, int maxSubSteps) {
    auto c                = config();
    c->fixedTimeStep      = seconds > 0.f ? seconds : 0.f;
    c->maxSubSteps        = maxSubSteps > 0 ? maxSubSteps : 1;
    sim()->fixedTimeAccum = 0.f;
}

float ParticleEmitter::getFixedTimeStep() { return config()->fixedTimeStep; }

void ParticleEmitter::setRandomSeed(int seed) {
    auto c            = config();
    auto s            = sim();
    c->randomSeed     = seed;
    c->autoRandomSeed = false;
    s->activeSeed     = seed;
    s->rng.seed(static_cast<std::mt19937::result_type>(seed));
}

int ParticleEmitter::getRandomSeed() { return config()->randomSeed; }

void ParticleEmitter::setAutoRandomSeed(bool enabled) { config()->autoRandomSeed = enabled; }
bool ParticleEmitter::getAutoRandomSeed() { return config()->autoRandomSeed; }

void ParticleEmitter::start() {
    auto c    = config();
    auto s    = sim();
    int  seed = c->randomSeed;
    if (c->autoRandomSeed) {
        std::random_device rd;
        seed = static_cast<int>(rd());
    }
    s->activeSeed = seed;
    s->rng.seed(static_cast<std::mt19937::result_type>(seed));
    s->active            = true;
    s->paused            = false;
    s->emitterAge        = 0.f;
    s->fixedTimeAccum    = 0.f;
    s->distanceEmitAccum = 0.f;
    for (auto& b : c->bursts) b.emitted = false;
    const float prewarm = c->prewarmSeconds;
    if (prewarm > 0.f) {
        constexpr float kPrewarmDt = 1.f / 60.f;
        const int       steps      = int(std::ceil(prewarm / kPrewarmDt));
        for (int i = 0; i < steps; ++i) stepEmitterSim(*c, *s, kPrewarmDt);
    }
}

void ParticleEmitter::stop() {
    auto s               = sim();
    s->active            = false;
    s->paused            = false;
    s->emitAccum         = 0.f;
    s->distanceEmitAccum = 0.f;
    s->fixedTimeAccum    = 0.f;
    s->emitterAge        = 0.f;
    s->lastX             = config()->x;
    s->lastY             = config()->y;
    s->hasLastPos        = true;
}

void ParticleEmitter::pause() {
    auto s = sim();
    if (s->active) s->paused = true;
}

void ParticleEmitter::reset() {
    auto s               = sim();
    s->alive             = 0;
    s->emitAccum         = 0.f;
    s->distanceEmitAccum = 0.f;
    s->fixedTimeAccum    = 0.f;
    s->emitterAge        = 0.f;
    s->hasLastPos        = false;
    s->overflowWarned    = false;
    for (auto& b : config()->bursts) b.emitted = false;
    for (auto& p : s->particles) p.life = 0.f;
    auto g = gpuSim();
    if (g->residentHandle != 0) {
        auto* gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (gfx) gfx->resetGpuParticleEmitter(g->residentHandle);
    }
    g->residentActive = false;
    g->estimatedAlive = 0;
    g->timeline       = 0.0;
    g->deathTimes.clear();
}

void ParticleEmitter::emit(int count) {
    if (count <= 0) return;
    auto c = config();
    auto s = sim();
    // Spawn at the CURRENT attached position so manual bursts do not lag a
    // frame behind animation or IK updates.
    syncAttach();
    for (int i = 0; i < count; ++i) spawnParticle(*c, *s);
}

bool ParticleEmitter::isActive() {
    auto s = sim();
    return s->active && !s->paused;
}
bool ParticleEmitter::isPaused() { return sim()->paused; }
bool ParticleEmitter::isStopped() { return !sim()->active; }

int ParticleEmitter::getCount() {
    auto g = gpuSim();
    return g->residentActive ? g->estimatedAlive + sim()->alive : sim()->alive;
}
int ParticleEmitter::getBufferSize() { return int(sim()->particles.size()); }

}  // namespace eve::particles
