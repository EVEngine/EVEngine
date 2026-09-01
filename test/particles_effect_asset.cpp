#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace eve::particles;

TEST_CASE("particles.effectAsset.versionedMultiEmitterContract") {
    const char* json = R"({
        "type": "eve.particle-effect",
        "version": 1,
        "parameters": { "intensity": 2.0 },
        "emitters": [
            {
                "name": "core",
                "offset": [10.0, 0.0],
                "rotation": 0.1,
                "parameters": { "intensity": 3.0 },
                "emitter": {
                    "buffer": 32,
                    "direction": 0.25,
                    "layer": 3,
                    "emissionRate": 0.0,
                    "parameterBindings": [
                        { "parameter": "intensity", "target": "spawnRate", "scale": 0.5 }
                    ]
                }
            },
            {
                "name": "haze",
                "enabled": false,
                "offset": [-4.0, 6.0],
                "emitter": { "buffer": 8, "emissionRate": 0.0, "layer": -2 }
            }
        ]
    })";

    auto*     particles = Particles::create();
    const int before    = particles->getEmitterCount();
    auto*     effect    = particles->newEffectFromText(json);
    REQUIRE(effect != nullptr);
    CHECK(particles->getLastEffectError().empty());
    CHECK_EQ(effect->getVersion(), 1);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK_EQ(effect->getEmitterName(0), std::string("core"));
    CHECK_EQ(effect->getEmitterName(1), std::string("haze"));
    CHECK(effect->getEmitter(2) == nullptr);

    auto* core = effect->getEmitterByName("core");
    auto* haze = effect->getEmitterByName("haze");
    REQUIRE(core != nullptr);
    REQUIRE(haze != nullptr);
    CHECK_EQ(core->getBufferSize(), 32);
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    effect->setPosition(100.f, 200.f);
    effect->setScale(2.f);
    effect->setRotation(1.57079632679f);
    CHECK(std::abs(core->getX() - 100.f) < 0.001f);
    CHECK(std::abs(core->getY() - 220.f) < 0.001f);
    CHECK(std::abs(core->getDirection() - 1.9207963f) < 0.001f);
    CHECK(std::abs(haze->getX() - 88.f) < 0.001f);
    CHECK(std::abs(haze->getY() - 192.f) < 0.001f);

    effect->setLayer(10);
    CHECK_EQ(core->getLayer(), 13);
    CHECK_EQ(haze->getLayer(), 8);
    effect->setVisible(false);
    CHECK(!core->isVisible());
    CHECK(!haze->isVisible());
    effect->setVisible(true);
    CHECK(core->isVisible());
    CHECK(!haze->isVisible());

    CHECK(effect->hasFloatParameter("intensity"));
    CHECK(std::abs(effect->getFloatParameter("intensity") - 2.f) < 0.001f);
    CHECK(std::abs(core->getFloatParameter("intensity") - 3.f) < 0.001f);
    effect->setFloatParameter("intensity", 4.f);
    CHECK(std::abs(core->getFloatParameter("intensity") - 4.f) < 0.001f);
    CHECK(std::abs(haze->getFloatParameter("intensity") - 4.f) < 0.001f);
    CHECK(std::abs(core->getResolvedParameterScale("spawnRate") - 2.f) < 0.001f);

    effect->start();
    CHECK(core->isActive());
    CHECK(!haze->isActive());
    CHECK(effect->emit("core", 3));
    CHECK(!effect->emit("missing", 3));
    effect->pause();
    CHECK(core->isPaused());
    effect->stop();
    CHECK(core->isStopped());

    delete effect;
    CHECK_EQ(particles->getEmitterCount(), before);
}

TEST_CASE("particles.effectAsset.rejectsUnknownVersionAndDuplicateNames") {
    auto* particles   = Particles::create();
    auto* unsupported = particles->newEffectFromText(
        R"({"type":"eve.particle-effect","version":2,"emitters":[{"name":"a","emitter":{}}]})");
    CHECK(unsupported == nullptr);
    CHECK(particles->getLastEffectError().find("unsupported") != std::string::npos);

    auto* duplicate = particles->newEffectFromText(R"({
        "type":"eve.particle-effect",
        "version":1,
        "emitters":[{"name":"a","emitter":{}},{"name":"a","emitter":{}}]
    })");
    CHECK(duplicate == nullptr);
    CHECK(particles->getLastEffectError().find("duplicate") != std::string::npos);
}

TEST_CASE("particles.effectAsset.playbackLabAssetIsConsumable") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/particle-playback-lab/impact.effect.json";
    std::ifstream     input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();

    std::string error;
    auto*       effect = ParticleEffect::fromText(text.str(), path, &error);
    REQUIRE(effect != nullptr);
    CHECK(error.empty());
    CHECK_EQ(effect->getSourcePath(), path);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK(effect->getEmitterByName("core") != nullptr);
    CHECK(effect->getEmitterByName("sparks") != nullptr);
    CHECK(effect->hasFloatParameter("intensity"));
    delete effect;
}
