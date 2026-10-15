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
