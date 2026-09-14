#include <algorithm>
#include <array>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Grass.h"
#include "graphics/GrassWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
namespace {
std::array<float, 32> snapshot(const eve::graphics::Shader& shader) {
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
TEST_CASE("graphics.GrassWind.scriptCheckedAdvanceApplyAndRollback") {
    using namespace eve::graphics;
    Shader shader;
    grass::bindDefaults(&shader);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    table.addClass<Shader>("Shader", std::function<Shader*()>([]() { return nullptr; }), true);
    exposeGrassWindBindings(table);
    vm.addFunc("shader", [&]() { return &shader; });
    vm.run(vm.compileSource(R"(
        local state=eve.VegetationWindState();
        local profile=eve.VegetationWindProfile();
        local tree=eve.TreeWindProfile();
        tree.setDimensions(2.0,6.0);tree.bendFactor=0.35;
        assert(!profile.enabled);
        assert(eve.initializeVegetationWind(state,0.6,0.8,0.0,2.0).ok);
        assert(state.getDirectionX()==0.6 && state.getDirectionY()==-0.8);
        assert(state.strength==1.2);
        assert(state.updatePhase==0.0);
        assert(eve.advanceVegetationWind(state,1.0,0.0,0.0,1.0,4.0).ok);
        assert(state.getDirectionX()==1.0 && state.getDirectionY()==-0.5);
        assert(state.getDirectionZ()==0.0 && state.strength==1.0);
        local audio=eve.VegetationWindAudioState();
        assert(eve.advanceVegetationWindAudio(audio,1.0,5.0,1.0,true,true).ok);
        assert(audio.volume==0.2 && audio.playing && audio.processing);
        local oldPhase=state.phase;
        assert(!eve.advanceVegetationWind(state,1.0,0.0,0.0,1.0,-1.0).ok);
        assert(state.phase==oldPhase);
        state.setDirection(1.0,-0.5,0.2);
        profile.enabled=true;
        profile.setFlex(0.8,1.15,0.1);
        profile.setFrequency(0.25,0.5,1.3);
        assert(eve.applyGrassWind(shader(),state,profile,2.0).ok);
        local wrongTree=eve.applyTreeWind(shader(),state,profile,tree,2.0);
        assert(!wrongTree.ok);
        profile.maximumDistance=0.0;
        assert(!eve.applyGrassWind(shader(),state,profile,4.0).ok);
        local badType=false;
        try { eve.applyGrassWind(null,state,profile,0.0); } catch(e) { badType=true; }
        assert(badType);
        badType=false;
        try { eve.advanceVegetationWind(null,0.0,0.0,0.0,0.0,0.0); } catch(e) { badType=true; }
        assert(badType);
    )"));
    CHECK(shader.pushConstantData()[23] == 100);
    CHECK(std::abs(shader.pushConstantData()[31] - 0.909297427F) < 0.000001F);
}
