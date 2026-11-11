// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"particles","default":"asset-files"}
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

TEST_CASE("particles.effectAsset.authoredGradientsSurviveLoading") {
    std::ifstream stream("examples/particle-effects/bonfire.effect.json", std::ios::binary);
    REQUIRE(stream.good());
    const std::string json{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    auto              parsed = eve::particles::ParticleEffect::tryFromText(json);
    REQUIRE(parsed.ok());
    std::unique_ptr<eve::particles::ParticleEffect> effect(parsed.value());
    auto*                                           core = effect->getEmitterByName("core");
    REQUIRE(core != nullptr);
    REQUIRE(!core->config()->colorGradient.empty());
    float r, g, b, a;
    core->config()->colorGradient.sample(0.35f, r, g, b, a);
    CHECK(std::fabs(r - 1.f) < 0.001f);
    CHECK(std::fabs(g - 0.45f) < 0.001f);
    CHECK(std::fabs(b - 0.08f) < 0.001f);
    core->config()->colorGradient.sample(1.f, r, g, b, a);
    CHECK_EQ(a, 0.f);
    SUB_CASE("Flame jet retains its warm gradient and transparent tail") {
        std::ifstream jetFile("examples/flame-jet/jet.effect.json", std::ios::binary);
        REQUIRE(jetFile.good());
        const std::string jetJson{std::istreambuf_iterator<char>(jetFile), std::istreambuf_iterator<char>()};
        auto              loaded = eve::particles::ParticleEffect::tryFromText(jetJson);
        REQUIRE(loaded.ok());
        std::unique_ptr<eve::particles::ParticleEffect> jet(loaded.value());
        auto*                                           flame = jet->getEmitterByName("flame");
        REQUIRE(flame != nullptr);
        REQUIRE(!flame->config()->colorGradient.empty());
        flame->config()->colorGradient.sample(.3f, r, g, b, a);
        REQUIRE_GT(r, .9f);
        REQUIRE_LT(g, .4f);
        REQUIRE_LT(b, .1f);
        flame->config()->colorGradient.sample(1.f, r, g, b, a);
        REQUIRE_EQ(a, 0.f);
        std::ifstream texture("examples/flame-jet/" + flame->resource()->texturePath, std::ios::binary);
        REQUIRE(texture.good());
    };
}
