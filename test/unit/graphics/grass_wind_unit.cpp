#include <algorithm>
#include <array>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Grass.h"
#include "graphics/GrassWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
namespace {
[[maybe_unused]] std::array<float, 32> snapshot(const eve::graphics::Shader& shader) {
    std::array<float, 32> result;
    std::copy_n(shader.pushConstantData(), 32, result.begin());
    return result;
}
}  // namespace
TEST_CASE("graphics.GrassWind.atomicSnapshotAndDisabledDefaults") {
    using namespace eve::graphics;
    Shader shader;
    grass::bindDefaults(&shader);
    REQUIRE(shader.usedFloats() == 32);
    CHECK(grass::paramCount() == 32);
    CHECK(grass::paramName(31) == "windSinFull");
    CHECK(shader.pushConstantData()[23] == 0);
    auto                  original = snapshot(shader);
    VegetationWindState   state{{1, -0.5F, 0.2F}, 0.8F, 0.17F};
    VegetationWindProfile profile;
    profile.enabled = true;
    auto packed     = packVegetationWind(state, profile, 2);
    REQUIRE(packed.ok());
    REQUIRE(applyGrassWind(shader, state, profile, 2).ok());
    for (int i = 0; i < 18; ++i) CHECK(shader.pushConstantData()[i] == original[i]);
    for (int i = 0; i < 14; ++i) CHECK(shader.pushConstantData()[i + 18] == packed.value()[i]);
    const auto before       = snapshot(shader);
    profile.maximumDistance = 0;
    CHECK(!applyGrassWind(shader, state, profile, 3).ok());
    CHECK(snapshot(shader) == before);
    profile.maximumDistance = 100;
    CHECK(!applyGrassWind(shader, state, profile, std::numeric_limits<double>::infinity()).ok());
    CHECK(snapshot(shader) == before);
    profile.enabled = false;
    REQUIRE(applyGrassWind(shader, state, profile, 3).ok());
    CHECK(shader.pushConstantData()[23] == 0);
}
TEST_CASE("graphics.VegetationWind.instantApplyIsDistinctAndAtomic") {
    eve::graphics::VegetationWindState state{{9, 8, 7}, 0.25F, 4};
    REQUIRE(eve::graphics::initializeVegetationWind(state, {0.6F, 0.8F, 0}, 2).ok());
    CHECK(state.direction == glm::vec3(0.6F, -0.8F, 0));
    CHECK(state.strength == 1.2F);
    CHECK(std::abs(state.phase - 0.1331F) < 0.000001F);
    CHECK(state.updatePhase == 0);
    REQUIRE(eve::graphics::advanceVegetationWind(state, {0.6F, 0.8F, 0}, 1.2F, 1).ok());
    CHECK(std::abs(state.phase - 0.1331F) < 0.000001F);
    CHECK(std::abs(state.updatePhase - 0.1331F) < 0.000001F);
    const auto before = state;
    CHECK(!eve::graphics::initializeVegetationWind(state, {std::numeric_limits<float>::infinity(), 0, 0}, 1).ok());
    CHECK(state.direction == before.direction);
    CHECK(state.strength == before.strength);
    CHECK(state.phase == before.phase);
    CHECK(state.updatePhase == before.updatePhase);
}
TEST_CASE("graphics.VegetationWind.audioRetainsSourceControllerSemantics") {
    using namespace eve::graphics;
    VegetationWindAudioState state;
    REQUIRE(advanceVegetationWindAudio(state, 1, 5, 1, true, true).ok());
    CHECK(state.volume == 0.2F);
    CHECK(state.anchorVolume == 0);
    CHECK(state.currentWindSpeed == 0);
    CHECK(state.processing);
    CHECK(state.playing);
    for (int i = 0; i < 20 && state.processing; ++i)
        REQUIRE(advanceVegetationWindAudio(state, 1, 5, 1, true, true).ok());
    CHECK(state.volume == 1);
    CHECK(!state.processing);
    REQUIRE(advanceVegetationWindAudio(state, 0, 5, 1, true, true).ok());
    CHECK(state.volume == 1);
    state.processing = true;
    state.anchorVolume = state.volume;
    REQUIRE(advanceVegetationWindAudio(state, 0, 5, 1, true, true).ok());
    CHECK(state.volume == 0.8F);
    CHECK(state.processing);
    const auto before = state;
    CHECK(!advanceVegetationWindAudio(state, 1, 0, 1, true, true).ok());
    CHECK(state.volume == before.volume);
    CHECK(state.processing == before.processing);
}
TEST_CASE("graphics.GrassWind.rejectsLateSchemaMismatchBeforeAnyWrite") {
    using namespace eve::graphics;
    Shader shader;
    for (int i = 0; i < 18; ++i) shader.declareFloat("prefix" + std::to_string(i));
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("wrongLastUniform");
    shader.sendVec4("windGlobals", 9, 8, 7, 6);
    const auto            before = snapshot(shader);
    VegetationWindState   state{{1, 0, 0}, 1, 0};
    VegetationWindProfile profile;
    profile.enabled = true;
    CHECK(!applyGrassWind(shader, state, profile, 0).ok());
    CHECK(snapshot(shader) == before);
}
