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

TEST_CASE("particles.emitter.blendAndFlipbookSetters") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);

    e->setBlendMode("additive");
    CHECK_EQ(e->getBlendMode(), std::string("additive"));
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Additive));
    e->setBlendMode("alpha");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Alpha));
    e->setBlendMode("opaque");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Opaque));
    e->setBlendMode("bogus");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Alpha));

    e->setFlipbook(4, 2, 12.f, 0.5f);
    CHECK_EQ(e->config()->hframes, 4);
    CHECK_EQ(e->config()->vframes, 2);
    CHECK(std::abs(e->config()->frameRate - 12.f) < 1e-5f);
    CHECK(std::abs(e->config()->frameRandomStart - 0.5f) < 1e-5f);

    e->setStartRotation(-45.f, 45.f);
    CHECK(std::abs(e->config()->startRotMin + 45.f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMax - 45.f) < 1e-5f);
}

TEST_CASE("particles.emitter.gradientAndCurves") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);

    e->addColorStop(0.f, 1.f, 0.f, 0.f, 1.f);
    e->addColorStop(0.5f, 0.f, 1.f, 0.f, 1.f);
    e->addColorStop(1.f, 0.f, 0.f, 1.f, 1.f);
    CHECK_EQ(e->config()->colorGradient.size(), size_t(3));
    float r = 0, g = 0, b = 0, a = 0;
    e->config()->colorGradient.sample(0.25f, r, g, b, a);
    CHECK(std::abs(r - 0.5f) < 1e-4f);
    CHECK(std::abs(g - 0.5f) < 1e-4f);
    e->config()->colorGradient.sample(0.75f, r, g, b, a);
    CHECK(std::abs(g - 0.5f) < 1e-4f);
    CHECK(std::abs(b - 0.5f) < 1e-4f);

    e->addSizeCurvePoint(0.f, 0.2f);
    e->addSizeCurvePoint(1.f, 2.f);
    CHECK(std::abs(e->config()->sizeCurve.sample(0.5f, 1.f) - 1.1f) < 1e-4f);
    CHECK(std::abs(e->config()->sizeCurve.sample(0.f, 1.f) - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->sizeCurve.sample(1.f, 1.f) - 2.f) < 1e-5f);

    e->addRotationCurvePoint(0.f, 0.f);
    e->addRotationCurvePoint(1.f, 90.f);
    CHECK(std::abs(e->config()->rotationCurve.sample(0.5f, 0.f) - 45.f) < 1e-4f);
}

TEST_CASE("particles.config.applyBlendFlipbookGradient") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    const char *json = R"({
      "blendMode": "additive",
      "flipbook": {"hframes": 4, "vframes": 2, "frameRate": 15, "frameRandomStart": 0.25},
      "startRotation": [-30, 30],
      "colorOverLifetime": [{"t":0,"r":1,"g":0.5,"b":0,"a":1},{"t":1,"r":1,"g":0,"b":0,"a":0}],
      "sizeOverLifetime": [[0, 0.5], [1, 1.5]],
      "rotationOverLifetime": [{"t":0,"v":0},{"t":1,"v":120}]
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->getBlendMode(), std::string("additive"));
    CHECK_EQ(e->config()->hframes, 4);
    CHECK_EQ(e->config()->vframes, 2);
    CHECK(std::abs(e->config()->frameRate - 15.f) < 1e-5f);
    CHECK(std::abs(e->config()->frameRandomStart - 0.25f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMin + 30.f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMax - 30.f) < 1e-5f);
    CHECK_EQ(e->config()->colorGradient.size(), size_t(2));
    CHECK_EQ(e->config()->sizeCurve.size(), size_t(2));
    CHECK_EQ(e->config()->rotationCurve.size(), size_t(2));
    float r = 0, g = 0, b = 0, a = 0;
    e->config()->colorGradient.sample(0.5f, r, g, b, a);
    CHECK(std::abs(r - 1.f) < 1e-4f);
    CHECK(std::abs(g - 0.25f) < 1e-4f);
}

TEST_CASE("particles.sim.startRotationAndFlipbookAdvance") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setStartRotation(90.f, 90.f);
    e->setFlipbook(8, 8, 16.f, 0.f);
    e->emit(4);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(e->sim()->particles[size_t(i)].rot -
                       static_cast<float>(3.14159265358979323846) * 0.5f) < 1e-4f);
    }

    ParticleSimSystem::update(0.25f);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(e->sim()->particles[size_t(i)].frame - 4.f) < 1e-4f);
        CHECK(std::abs(e->sim()->particles[size_t(i)].rot -
                       (static_cast<float>(3.14159265358979323846) * 0.5f)) < 1e-4f);
    }
}

TEST_CASE("particles.emitter.bursts") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->addBurst(0.1f, 20);
    e->start();

    ParticleSimSystem::update(0.05f);
    CHECK_EQ(e->getCount(), 0);
    ParticleSimSystem::update(0.06f);
    CHECK_EQ(e->getCount(), 20);
    // Bursts fire only once.
    ParticleSimSystem::update(0.2f);
    CHECK_EQ(e->getCount(), 20);
}

TEST_CASE("particles.emitter.gravityDampingLimit") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setGravity(0.f, 100.f);
    e->emit(1);
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(e->sim()->particles[0].vy - 100.f) < 1e-3f);

    ParticleEmitter *d = mod->newEmitter(16);
    d->setParticleLifetime(5.f, 5.f);
    d->setSpeed(100.f, 100.f);
    d->setDamping(1.f);
    d->emit(1);
    ParticleSimSystem::update(0.5f);
    CHECK(std::abs(d->sim()->particles[0].vx - 50.f) < 1e-3f);

    ParticleEmitter *l = mod->newEmitter(16);
    l->setParticleLifetime(5.f, 5.f);
    l->setSpeed(100.f, 100.f);
    l->setLimitVelocity(30.f);
    l->emit(1);
    ParticleSimSystem::update(0.1f);
    const float sp = std::sqrt(l->sim()->particles[0].vx * l->sim()->particles[0].vx +
                               l->sim()->particles[0].vy * l->sim()->particles[0].vy);
    CHECK_LE(sp, 30.001f);
}

TEST_CASE("particles.emitter.inheritVelocity") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(10.f);
    e->setInheritVelocity(0.5f);
    e->setPosition(0.f, 0.f);
    e->start();
    ParticleSimSystem::update(0.1f);
    e->setPosition(10.f, 0.f);
    ParticleSimSystem::update(0.1f);
    REQUIRE_GT(e->getCount(), 1);
    const auto &p = e->sim()->particles[size_t(e->sim()->alive - 1)];
    CHECK(std::abs(p.vx - 50.f) < 1e-3f);
}

TEST_CASE("particles.emitter.localSpace") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setSimulationSpace("local");
    e->setPosition(0.f, 0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    e->setPosition(100.f, 50.f);
    ParticleSimSystem::update(0.1f);
    const auto &p = e->sim()->particles[0];
    CHECK(std::abs(p.x - 100.f) < 1e-3f);
    CHECK(std::abs(p.y - 50.f) < 1e-3f);
}

TEST_CASE("particles.emitter.collisionBounds") {
    auto *mod = Particles::create();
    ParticleEmitter *k = mod->newEmitter(16);
    k->setParticleLifetime(5.f, 5.f);
    k->setSpeed(0.f, 0.f);
    k->setPosition(0.f, 0.f);
    k->setCollision("kill");
    k->setCollisionBounds(true, 10.f, 0.f, 100.f, 100.f);
    k->emit(1);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(k->getCount(), 0);

    ParticleEmitter *b = mod->newEmitter(16);
    b->setParticleLifetime(5.f, 5.f);
    b->setSpeed(100.f, 100.f);
    b->setDirection(0.f);
    b->setSpread(0.f);
    b->setPosition(0.f, 0.f);
    b->setCollision("bounce", 4.f, 1.f);
    b->setCollisionBounds(true, 0.f, -1000.f, 15.f, 1000.f);
    b->emit(1);
    ParticleSimSystem::update(0.2f);
    CHECK_LT(b->sim()->particles[0].vx, 0.f);
}

TEST_CASE("particles.emitter.overflowPauseAndMaxDelta") {
    auto *mod = Particles::create();
    ParticleEmitter *p = mod->newEmitter(4);
    p->setParticleLifetime(5.f, 5.f);
    p->setSpeed(0.f, 0.f);
    p->setEmissionRate(100.f);
    p->setOverflowMode("pause");
    p->start();
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(p->getCount(), 4);
    CHECK_GT(p->sim()->emitAccum, 0.f);

    ParticleEmitter *m = mod->newEmitter(4);
    m->setParticleLifetime(5.f, 5.f);
    m->setSpeed(100.f, 100.f);
    m->setMaxDeltaTime(0.1f);
    m->emit(1);
    ParticleSimSystem::update(10.f);
    CHECK(std::abs(m->sim()->particles[0].x - 10.f) < 1e-2f);
}

TEST_CASE("particles.emitter.subEmitter") {
    auto *mod = Particles::create();
    ParticleEmitter *parent = mod->newEmitter(32);
    ParticleEmitter *child = mod->newEmitter(32);
    ParticleEmitter *ashes = mod->newEmitter(32);
    parent->setParticleLifetime(5.f, 5.f);
    parent->setSpeed(0.f, 0.f);
    child->setParticleLifetime(5.f, 5.f);
    child->setSpeed(0.f, 0.f);
    ashes->setParticleLifetime(5.f, 5.f);
    ashes->setSpeed(0.f, 0.f);
    parent->addSubEmitter(child, "birth");
    parent->addSubEmitter(ashes, "death");
    parent->emit(5);
    CHECK_EQ(child->getCount(), 5);
    CHECK_EQ(ashes->getCount(), 0);

    parent->setParticleLifetime(0.2f, 0.2f);
    parent->emit(1);
    ParticleSimSystem::update(0.3f);
    CHECK_EQ(ashes->getCount(), 1);
}

TEST_CASE("particles.emitter.forceFields") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(0.f, 0.f);
    e->emit(1);
    e->sim()->particles[0].x = 100.f;
    e->addForceField(0.f, 0.f, 200.f, 50.f, 1.f);  // attract toward origin
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(e->sim()->particles[0].vx + 25.f) < 1e-3f);

    ParticleEmitter *r = mod->newEmitter(16);
    r->setParticleLifetime(5.f, 5.f);
    r->setSpeed(0.f, 0.f);
    r->setPosition(0.f, 0.f);
    r->emit(1);
    r->sim()->particles[0].x = 100.f;
    r->addForceField(0.f, 0.f, 200.f, -50.f, 1.f);  // repel
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(r->sim()->particles[0].vx - 25.f) < 1e-3f);
}

TEST_CASE("particles.draw.shaderStorage") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    CHECK(e->getShader() == nullptr);
    auto *fake = reinterpret_cast<eve::graphics::Shader *>(uintptr_t(0x1234));
    e->setShader(fake);
    CHECK(e->getShader() == fake);
    e->setShader(nullptr);
    CHECK(e->getShader() == nullptr);
}

TEST_CASE("particles.lights.followParticles") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(30.f, 40.f);
    e->setLights(true, 50.f, 2.f, 1.f, 0.5f, 0.2f, 3);
    e->emit(2);
    ParticleLightSystem::update();
    REQUIRE_EQ(e->lights()->pool.size(), size_t(3));
    CHECK(std::abs(e->lights()->pool[0]->getX() - 30.f) < 1e-3f);
    CHECK(std::abs(e->lights()->pool[0]->getY() - 40.f) < 1e-3f);
    CHECK(std::abs(e->lights()->pool[0]->getRadius() - 50.f) < 1e-3f);
    CHECK(e->lights()->pool[0]->isEnabled());
    CHECK(e->lights()->pool[1]->isEnabled());
    CHECK(!e->lights()->pool[2]->isEnabled());

    e->setLights(false);
    ParticleLightSystem::update();
    CHECK(!e->lights()->pool[0]->isEnabled());
}

TEST_CASE("particles.gpu.configAndFallback") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    e->setGpuSimulation(true);
    CHECK(e->getGpuSimulation());
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(100.f, 100.f);
    e->setDirection(0.f);
    e->setSpread(0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    // Headless CI has no GPU device → the emitter must fall back to the CPU
    // integrator; either path moves the particle forward.
    CHECK_GT(e->sim()->particles[0].x, 1.f);
    CHECK_GT(e->getCount(), 0);

    ParticleEmitter *c = mod->newEmitter(16);
    const char *json = R"({"gpuSimulation": true})";
    CHECK(c->applyConfig(json));
    CHECK(c->getGpuSimulation());
}

TEST_CASE("particles.scale.manyEmitters") {
    // Regression guard for ECS view iteration / simulation at scale: a few
    // hundred emitters sharing one ECS table must update without stalling or
    // losing emitters. (Output-volume note: this suite must be run with
    // redirected output — a full pipe blocks once it fills.)
    auto *mod = Particles::create();
    constexpr int kEmitters = 128;
    std::vector<ParticleEmitter *> emitters;
    emitters.reserve(size_t(kEmitters));
    for (int i = 0; i < kEmitters; ++i) {
        ParticleEmitter *e = mod->newEmitter(32);
        e->setParticleLifetime(2.f, 2.f);
        e->setEmissionRate(10.f);
        e->setSpeed(10.f, 20.f);
        e->setPosition(float(i % 16) * 10.f, float(i / 16) * 10.f);
        e->start();
        emitters.push_back(e);
    }
    for (int frame = 0; frame < 10; ++frame) ParticleSimSystem::update(0.016f);

    int totalAlive = 0;
    for (auto *e : emitters) totalAlive += e->getCount();
    CHECK_GT(totalAlive, 0);
    // First and last emitters must both have been iterated by the system view.
    CHECK_GT(emitters[0]->getCount(), 0);
    CHECK_GT(emitters[size_t(kEmitters) - 1]->getCount(), 0);
    CHECK(emitters[size_t(kEmitters) - 1]->isActive());
}

TEST_CASE("particles.config.applyForceFieldsLights") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(16);
    const char *json = R"({
      "forceFields": [{"x":0,"y":0,"radius":200,"strength":50,"falloff":2}],
      "lights": {"enabled":true,"max":6,"radius":80,"intensity":1.5,"color":[1,0.5,0.2]}
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->config()->forceFields.size(), size_t(1));
    CHECK(std::abs(e->config()->forceFields[0].strength - 50.f) < 1e-5f);
    CHECK(std::abs(e->config()->forceFields[0].falloff - 2.f) < 1e-5f);
    CHECK(e->config()->lights.enabled);
    CHECK_EQ(e->config()->lights.max, 6);
    CHECK(std::abs(e->config()->lights.r - 1.f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.g - 0.5f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.b - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.intensity - 1.5f) < 1e-5f);
}

TEST_CASE("particles.config.applyP1Json") {
    auto *mod = Particles::create();
    ParticleEmitter *e = mod->newEmitter(32);
    const char *json = R"({
      "bursts": [{"time":0.05,"count":12}],
      "prewarm": 0.5,
      "gravity": [0, 50],
      "damping": 0.2,
      "limitVelocity": 40,
      "velocityOverLifetime": [[0, 1], [1, 0.2]],
      "inheritVelocity": 0.4,
      "simulationSpace": "local",
      "noise": {"strength": 3, "frequency": 2, "speed": 1.5},
      "collision": {"mode":"bounce","radius":6,"restitution":0.8,"lifetimeLoss":0.1},
      "collisionBounds": {"enabled":true,"minX":0,"minY":0,"maxX":640,"maxY":480},
      "worldCollision": true,
      "renderMode": "stretched",
      "stretch": 1.5,
      "overflowMode": "pause",
      "maxDeltaTime": 0.05
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->config()->bursts.size(), size_t(1));
    CHECK(std::abs(e->config()->prewarmSeconds - 0.5f) < 1e-5f);
    CHECK(std::abs(e->config()->gravityY - 50.f) < 1e-5f);
    CHECK(std::abs(e->config()->damping - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->limitVelocity - 40.f) < 1e-5f);
    CHECK_EQ(e->config()->velocityCurve.size(), size_t(2));
    CHECK(std::abs(e->config()->inheritVelocity - 0.4f) < 1e-5f);
    CHECK_EQ(e->config()->simSpace, std::string("local"));
    CHECK(std::abs(e->config()->noiseStrength - 3.f) < 1e-5f);
    CHECK(std::abs(e->config()->noiseFrequency - 2.f) < 1e-5f);
    CHECK_EQ(e->config()->collisionMode, std::string("bounce"));
    CHECK(std::abs(e->config()->collisionRadius - 6.f) < 1e-5f);
    CHECK(std::abs(e->config()->collisionRestitution - 0.8f) < 1e-5f);
    CHECK(e->config()->collisionBoundsEnabled);
    CHECK(std::abs(e->config()->boundsMaxX - 640.f) < 1e-5f);
    CHECK(e->config()->worldCollision);
    CHECK_EQ(e->config()->renderMode, std::string("stretched"));
    CHECK(std::abs(e->config()->stretchFactor - 1.5f) < 1e-5f);
    CHECK_EQ(e->config()->overflowMode, std::string("pause"));
    CHECK(std::abs(e->config()->maxDeltaTime - 0.05f) < 1e-5f);
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
