#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"

#include <cmath>
#include <string>

using namespace eve::particles;

TEST_CASE("particles.renderer.facingAndSortContract") {
    auto* emitter = Particles::create()->newEmitter(32);

    emitter->setRenderMode("axis");
    emitter->setRenderAxis(37.5f);
    emitter->setSortMode("distance");
    CHECK_EQ(emitter->config()->renderMode, std::string("axis"));
    CHECK(std::abs(emitter->config()->renderAxisDegrees - 37.5f) < 1e-5f);
    CHECK_EQ(emitter->getSortMode(), std::string("distance"));

    emitter->setRenderMode("velocity", 2.25f);
    CHECK_EQ(emitter->config()->renderMode, std::string("stretched"));
    CHECK(std::abs(emitter->config()->stretchFactor - 2.25f) < 1e-5f);

    emitter->setSortMode("unsupported");
    CHECK_EQ(emitter->getSortMode(), std::string("none"));

    emitter->setRibbon(0.65f, 3.f);
    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK(std::abs(emitter->config()->ribbonWidth - 0.65f) < 1e-5f);
    CHECK(std::abs(emitter->config()->ribbonMinSegmentLength - 3.f) < 1e-5f);

    emitter->setSoftParticles(true, 1.2f, 0.f);
    CHECK(emitter->config()->softParticles);
    CHECK(std::abs(emitter->config()->softParticleDepth - 1.f) < 1e-5f);
    CHECK(emitter->config()->softFadeDistance >= 1e-5f);
    CHECK(!emitter->isSoftParticlesActive());

    emitter->setMaterialMode("lit");
    CHECK_EQ(emitter->getMaterialMode(), std::string("lit"));
    emitter->setGpuSimulation(true);
    CHECK(!emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getSimulationBackend(), std::string("cpu"));
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("lit_material"));
    emitter->setMaterialMode("unsupported");
    CHECK_EQ(emitter->getMaterialMode(), std::string("unlit"));
    CHECK(emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("backend_unavailable"));
}

TEST_CASE("particles.renderer.jsonContract") {
    auto* emitter = Particles::create()->newEmitter(32);
    REQUIRE(emitter->applyConfig(R"({
        "renderMode": "axis",
        "renderAxis": -22.5,
        "sortMode": "youngest",
        "ribbon": {"width": 0.75, "minSegmentLength": 2.5},
        "softParticles": {"enabled": true, "depth": 0.42, "fadeDistance": 0.08},
        "material": {"mode": "lit", "normalTexture": "particles/test-normal.png"}
    })"));

    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK(std::abs(emitter->config()->renderAxisDegrees + 22.5f) < 1e-5f);
    CHECK_EQ(emitter->getSortMode(), std::string("youngest"));
    CHECK(std::abs(emitter->config()->ribbonWidth - 0.75f) < 1e-5f);
    CHECK(emitter->config()->softParticles);
    CHECK(std::abs(emitter->config()->softParticleDepth - 0.42f) < 1e-5f);
    CHECK_EQ(emitter->getMaterialMode(), std::string("lit"));
    CHECK_EQ(emitter->resource()->normalTexturePath, std::string("particles/test-normal.png"));
}
