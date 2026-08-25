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
}

TEST_CASE("particles.renderer.jsonContract") {
    auto* emitter = Particles::create()->newEmitter(32);
    REQUIRE(emitter->applyConfig(R"({
        "renderMode": "axis",
        "renderAxis": -22.5,
        "sortMode": "youngest",
        "ribbon": {"width": 0.75, "minSegmentLength": 2.5}
    })"));

    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK(std::abs(emitter->config()->renderAxisDegrees + 22.5f) < 1e-5f);
    CHECK_EQ(emitter->getSortMode(), std::string("youngest"));
    CHECK(std::abs(emitter->config()->ribbonWidth - 0.75f) < 1e-5f);
}
