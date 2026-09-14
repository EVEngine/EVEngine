#include "zeroerr/unittest.h"
#include "zeroerr/assert.h"

#include "stylize/MeshParticleEmitter.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"

using namespace eve::stylize;

TEST_CASE("stylize.meshParticles.fixedStepDeterminismAndBurst") {
    MeshParticleEmitterConfig config;
    config.capacity = 32;
    config.emissionRate = 10.f;
    config.lifetimeMin = 2.f;
    config.lifetimeMax = 2.f;
    config.speedMin = 1.f;
    config.speedMax = 1.f;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps = 8;
    config.randomSeed = 42;
    config.bursts = {{0.2f, 3}};

    MeshParticleEmitter first(config);
    MeshParticleEmitter second(config);
    first.start();
    second.start();
    auto firstReport = first.advance(0.3f);
    auto secondReport = second.advance(0.1f);
    secondReport = second.advance(0.2f);
    REQUIRE_EQ(firstReport.alive, 6u);
    REQUIRE_EQ(first.snapshot().size(), second.snapshot().size());
    const auto a = first.snapshot();
    const auto b = second.snapshot();
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE_EQ(a[i].stableId, b[i].stableId);
        REQUIRE_EQ(a[i].model, b[i].model);
        REQUIRE_EQ(a[i].color, b[i].color);
    }
}

TEST_CASE("stylize.meshParticles.capacityReportsDroppedSpawns") {
    MeshParticleEmitterConfig config;
    config.capacity = 4;
    config.fixedStepSeconds = 0.1f;
    config.bursts = {{0.f, 7}};
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.1f);
    REQUIRE_EQ(report.spawned, 4u);
    REQUIRE_EQ(report.dropped, 3u);
    REQUIRE_EQ(report.alive, 4u);
}

TEST_CASE("stylize.meshParticles.substepGuardReportsDiscardedTime") {
    MeshParticleEmitterConfig config;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps = 2;
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.55f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    REQUIRE(report.discardedTimeSeconds > 0.29f);
    REQUIRE(report.discardedTimeSeconds < 0.31f);
}

TEST_CASE("stylize.meshParticles.bridgeUsesSharedLodAndAlphaOrdering") {
    MeshParticleEmitterConfig config;
    config.capacity = 3;
    config.fixedStepSeconds = 0.1f;
    MeshParticleEmitter emitter(config);
    emitter.setOrigin({0.f, 0.f, 5.f});
    const auto emitted = emitter.emit(3);
    REQUIRE(emitted == std::size_t{3});
    MeshEffectInstance effect("slash");
    const MeshVfxBatchKey key{11, 12, 13, 0, MeshVfxBatchBlend::Alpha};
    const auto inputs = buildMeshParticleRenderInputs(
        emitter.snapshot(), &effect, reinterpret_cast<eve::graphics::Mesh*>(1), nullptr,
        key, 77, glm::vec3(0.f), 100.f, 50.f, 2);
    REQUIRE_EQ(inputs.lodCandidates.size(), 3u);
    REQUIRE_EQ(inputs.renderItems.size(), 3u);
    REQUIRE_EQ(inputs.commands.size(), 3u);
    REQUIRE_EQ(inputs.commands[0].stableInstanceId, inputs.renderItems[0].stableInstanceId);
    REQUIRE_EQ(static_cast<int>(inputs.commands[0].kind),
               static_cast<int>(MeshVfxRendererCommand::Kind::Particle));

    MeshVfxScalabilityPolicy policy;
    policy.maximumDistance = 50.f;
    policy.workUnitBudget = 12;
    const auto decisions = MeshVfxScalabilityPlanner(policy).plan(inputs.lodCandidates);
    const auto queue = MeshVfxRenderBatchPlanner{}.build(inputs.renderItems, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.batches.size(), 1u);
}

TEST_CASE("stylize.meshParticles.curvesDriveScaleColorAndRotation") {
    MeshParticleEmitterConfig config;
    config.capacity = 1;
    config.lifetimeMin = 1.f;
    config.lifetimeMax = 1.f;
    config.fixedStepSeconds = 0.25f;
    config.rotationMinRadians = 0.f;
    config.rotationMaxRadians = 0.f;
    config.angularVelocityMin = 2.f;
    config.angularVelocityMax = 2.f;
    config.scaleCurve = {{1.f, 0.f}, {0.f, 2.f}, {0.5f, 1.f}};
    config.colorGradient = {{0.f, {1.f, 0.f, 0.f, 1.f}},
                            {1.f, {0.f, 0.f, 1.f, 0.f}}};
    MeshParticleEmitter emitter(config);
    const auto emitted = emitter.emit(1);
    REQUIRE(emitted == std::size_t{1});
    const auto report = emitter.advance(0.5f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    const auto particles = emitter.snapshot();
    REQUIRE_EQ(particles.size(), 1u);
    REQUIRE(particles[0].color.r > 0.49f);
    REQUIRE(particles[0].color.r < 0.51f);
    REQUIRE(particles[0].color.b > 0.49f);
    REQUIRE(particles[0].color.b < 0.51f);
    const float axisScale = glm::length(glm::vec3(particles[0].model[0]));
    REQUIRE(axisScale > 0.99f);
    REQUIRE(axisScale < 1.01f);
}

TEST_CASE("stylize.meshParticles.volumeShapesStayInsideConfiguredBounds") {
    MeshParticleEmitterConfig sphere;
    sphere.capacity = 64;
    sphere.shape = MeshParticleShape::Sphere;
    sphere.sphereRadius = 3.f;
    sphere.randomSeed = 7;
    MeshParticleEmitter sphereEmitter(sphere);
    const auto sphereEmitted = sphereEmitter.emit(64);
    REQUIRE(sphereEmitted == std::size_t{64});
    for (const auto& particle : sphereEmitter.snapshot()) {
        REQUIRE(glm::length(glm::vec3(particle.model[3])) <= 3.0001f);
    }

    MeshParticleEmitterConfig box;
    box.capacity = 64;
    box.shape = MeshParticleShape::Box;
    box.boxExtents = {1.f, 2.f, 3.f};
    box.randomSeed = 9;
    MeshParticleEmitter boxEmitter(box);
    const auto boxEmitted = boxEmitter.emit(64);
    REQUIRE(boxEmitted == std::size_t{64});
    for (const auto& particle : boxEmitter.snapshot()) {
        const glm::vec3 position(particle.model[3]);
        REQUIRE(std::abs(position.x) <= 1.0001f);
        REQUIRE(std::abs(position.y) <= 2.0001f);
        REQUIRE(std::abs(position.z) <= 3.0001f);
    }
}
