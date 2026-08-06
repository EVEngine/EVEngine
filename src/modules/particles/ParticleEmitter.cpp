#include "particles/ParticleEmitter.h"
#include "particles/ParticleConfig.h"

#include <cmath>
#include <random>

namespace eve::particles {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float randRange(std::mt19937 &rng, float a, float b) {
    if (a == b) return a;
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng);
}

}  // namespace

void spawnParticle(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim) {
    if (sim.alive >= int(sim.particles.size())) return;

    Particle &p = sim.particles[size_t(sim.alive++)];
    p.x = cfg.x;
    p.y = cfg.y;
    p.lifetime = randRange(sim.rng, cfg.lifeMin, cfg.lifeMax);
    if (p.lifetime <= 0.f) p.lifetime = 1e-4f;
    p.life = p.lifetime;
    p.ax = randRange(sim.rng, cfg.accelXMin, cfg.accelXMax);
    p.ay = randRange(sim.rng, cfg.accelYMin, cfg.accelYMax);

    const float half = cfg.spread * 0.5f;
    const float angle = cfg.direction + randRange(sim.rng, -half, half);
    const float speed = randRange(sim.rng, cfg.speedMin, cfg.speedMax);
    p.vx = std::cos(angle) * speed;
    p.vy = std::sin(angle) * speed;
    p.size = cfg.sizeStart;
}

void stepEmitterSim(ParticleEmitter::Config &cfg, ParticleEmitter::Sim &sim, float dt) {
    if (sim.paused || dt <= 0.f) return;

    int write = 0;
    for (int i = 0; i < sim.alive; ++i) {
        Particle &p = sim.particles[size_t(i)];
        p.life -= dt;
        if (p.life <= 0.f) continue;

        p.vx += p.ax * dt;
        p.vy += p.ay * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;

        if (write != i) sim.particles[size_t(write)] = p;
        ++write;
    }
    sim.alive = write;

    if (!sim.active) return;

    if (cfg.emitterLife >= 0.f) {
        sim.emitterAge += dt;
        if (sim.emitterAge >= cfg.emitterLife) {
            sim.active = false;
            sim.emitAccum = 0.f;
            return;
        }
    }

    if (cfg.emissionRate <= 0.f) return;
    sim.emitAccum += cfg.emissionRate * dt;
    while (sim.emitAccum >= 1.f) {
        spawnParticle(cfg, sim);
        sim.emitAccum -= 1.f;
        if (sim.alive >= int(sim.particles.size())) {
            sim.emitAccum = 0.f;
            break;
        }
    }
}

ParticleEmitter *ParticleEmitter::createEmitter(int bufferSize) {
    ParticleEmitter *e = ParticleEmitter::create();
    e->config()->entity = e;
    const int n = bufferSize > 0 ? bufferSize : 1;
    e->sim()->particles.resize(size_t(n));
    std::random_device rd;
    e->sim()->rng.seed(rd());
    return e;
}

void ParticleEmitter::setPosition(float x, float y) {
    config()->x = x;
    config()->y = y;
}

void ParticleEmitter::moveTo(float x, float y) { setPosition(x, y); }

float ParticleEmitter::getX() { return config()->x; }
float ParticleEmitter::getY() { return config()->y; }

void ParticleEmitter::setEmissionRate(float rate) {
    config()->emissionRate = rate < 0.f ? 0.f : rate;
}

float ParticleEmitter::getEmissionRate() { return config()->emissionRate; }

void ParticleEmitter::setParticleLifetime(float minLife, float maxLife) {
    auto c = config();
    c->lifeMin = minLife < 0.f ? 0.f : minLife;
    c->lifeMax = maxLife < c->lifeMin ? c->lifeMin : maxLife;
}

float ParticleEmitter::getParticleLifetimeMin() { return config()->lifeMin; }
float ParticleEmitter::getParticleLifetimeMax() { return config()->lifeMax; }

void ParticleEmitter::setEmitterLifetime(float seconds) { config()->emitterLife = seconds; }
float ParticleEmitter::getEmitterLifetime() { return config()->emitterLife; }

void ParticleEmitter::setDirection(float radians) { config()->direction = radians; }
float ParticleEmitter::getDirection() { return config()->direction; }

void ParticleEmitter::setSpread(float radians) {
    config()->spread = radians < 0.f ? 0.f : radians;
}
float ParticleEmitter::getSpread() { return config()->spread; }

void ParticleEmitter::setSpeed(float minSpeed, float maxSpeed) {
    auto c = config();
    c->speedMin = minSpeed;
    c->speedMax = maxSpeed < minSpeed ? minSpeed : maxSpeed;
}

void ParticleEmitter::setLinearAcceleration(float xmin, float ymin, float xmax, float ymax) {
    auto c = config();
    c->accelXMin = xmin;
    c->accelYMin = ymin;
    c->accelXMax = xmax < xmin ? xmin : xmax;
    c->accelYMax = ymax < ymin ? ymin : ymax;
}

void ParticleEmitter::setParticleSize(float width, float height) {
    auto c = config();
    c->particleW = width > 0.f ? width : 1.f;
    c->particleH = height > 0.f ? height : 1.f;
}

float ParticleEmitter::getParticleWidth() { return config()->particleW; }
float ParticleEmitter::getParticleHeight() { return config()->particleH; }

void ParticleEmitter::setSizes(float startScale, float endScale) {
    config()->sizeStart = startScale;
    config()->sizeEnd = endScale;
}

void ParticleEmitter::setColorStart(float r, float g, float b, float a) {
    config()->colorStart = Color(r, g, b, a);
}

void ParticleEmitter::setColorEnd(float r, float g, float b, float a) {
    config()->colorEnd = Color(r, g, b, a);
}

void ParticleEmitter::setTexture(graphics::Texture *texture) { draw()->texture = texture; }
graphics::Texture *ParticleEmitter::getTexture() { return draw()->texture; }

void ParticleEmitter::setLayer(int layer) { draw()->layer = layer; }
int ParticleEmitter::getLayer() { return draw()->layer; }

void ParticleEmitter::setVisible(bool visible) { draw()->visible = visible; }
bool ParticleEmitter::isVisible() { return draw()->visible; }

void ParticleEmitter::start() {
    auto s = sim();
    s->active = true;
    s->paused = false;
    s->emitterAge = 0.f;
}

void ParticleEmitter::stop() {
    auto s = sim();
    s->active = false;
    s->paused = false;
    s->emitAccum = 0.f;
    s->emitterAge = 0.f;
}

void ParticleEmitter::pause() {
    auto s = sim();
    if (s->active) s->paused = true;
}

void ParticleEmitter::reset() {
    auto s = sim();
    s->alive = 0;
    s->emitAccum = 0.f;
    s->emitterAge = 0.f;
    for (auto &p : s->particles) p.life = 0.f;
}

void ParticleEmitter::emit(int count) {
    if (count <= 0) return;
    auto c = config();
    auto s = sim();
    for (int i = 0; i < count; ++i) spawnParticle(*c, *s);
}

bool ParticleEmitter::isActive() {
    auto s = sim();
    return s->active && !s->paused;
}
bool ParticleEmitter::isPaused() { return sim()->paused; }
bool ParticleEmitter::isStopped() { return !sim()->active; }

int ParticleEmitter::getCount() { return sim()->alive; }
int ParticleEmitter::getBufferSize() { return int(sim()->particles.size()); }

void ParticleEmitter::applyPreset(const std::string &name) {
    if (name == "spark") {
        setEmissionRate(80.f);
        setParticleLifetime(0.2f, 0.6f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(kPi * 0.6f);
        setSpeed(60.f, 180.f);
        setLinearAcceleration(-20.f, 40.f, 20.f, 120.f);
        setParticleSize(4.f, 4.f);
        setSizes(1.f, 0.2f);
        setColorStart(1.f, 0.9f, 0.3f, 1.f);
        setColorEnd(1.f, 0.2f, 0.f, 0.f);
    } else if (name == "smoke") {
        setEmissionRate(25.f);
        setParticleLifetime(1.5f, 3.f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(0.4f);
        setSpeed(10.f, 40.f);
        setLinearAcceleration(-5.f, -30.f, 5.f, -10.f);
        setParticleSize(16.f, 16.f);
        setSizes(0.5f, 2.f);
        setColorStart(0.5f, 0.5f, 0.5f, 0.5f);
        setColorEnd(0.3f, 0.3f, 0.3f, 0.f);
    } else if (name == "fire") {
        setEmissionRate(60.f);
        setParticleLifetime(0.4f, 1.0f);
        setEmitterLifetime(-1.f);
        setDirection(-kPi * 0.5f);
        setSpread(0.5f);
        setSpeed(20.f, 80.f);
        setLinearAcceleration(-15.f, -80.f, 15.f, -20.f);
        setParticleSize(10.f, 10.f);
        setSizes(1.2f, 0.3f);
        setColorStart(1.f, 0.7f, 0.1f, 1.f);
        setColorEnd(1.f, 0.1f, 0.f, 0.f);
    }
}

bool ParticleEmitter::applyConfig(const std::string &json) {
    return applyConfigText(this, json, nullptr);
}

bool ParticleEmitter::loadConfig(const std::string &path) {
    return loadConfigFile(this, path, nullptr);
}

bool ParticleEmitter::reloadConfig() { return reloadConfigFile(this, nullptr); }

void ParticleEmitter::setAutoReload(bool enable) { resource()->autoReload = enable; }
bool ParticleEmitter::getAutoReload() { return resource()->autoReload; }
std::string ParticleEmitter::getConfigPath() { return resource()->path; }

}  // namespace eve::particles
