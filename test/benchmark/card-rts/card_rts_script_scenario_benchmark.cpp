// Opt-in full-match scenario: sustained collection, combat, and victory belong to benchmarking.
#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "PathBesideSource.h"
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_card_rts_full_match_benchmark {
namespace {
std::string readScenario(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, const std::string& name) {
    const auto root = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    std::ifstream stream(root / "examples" / "rts-cardfront" / name, std::ios::binary);
    REQUIRE(stream.is_open());
    std::ostringstream content;
    content << stream.rdbuf();
    return content.str();
}
}  // namespace

TEST_CASE("card.rtsScriptScenarioCollectsBuildsProducesAndPlaysAtomicSkills") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(8192, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource("transaction <- eve.Transaction();"));
    vm.set("contentJson", readScenario(_ZEROERR_TEST_CONTEXT, "data/content.json"));
    vm.set("cardsJson", readScenario(_ZEROERR_TEST_CONTEXT, "data/cards.json"));
    vm.run(vm.compileSource(R"(
        eve_init <- null; eve_update <- null; eve_render <- null; eve_reload <- null;
        gfx <- { setBackgroundColor=function(r,g,b,a){} };
        fs <- { readText=function(path){ return path=="data/content.json"?contentJson:cardsJson; } };
    )"));
    const auto scenario = readScenario(_ZEROERR_TEST_CONTEXT, "main.nut");
    vm.run(vm.compileSource(scenario.c_str()));
    vm.run(vm.compileSource(R"(
        initPresentation=function() {}; eve_init();
        local untouchedMana=mission.mana;
        playNamed("grenade",23.5,8.0); // No caster: no payment/card loss.
        assert(hand.count()==4 && mission.mana==untouchedMana && mission.skillPlays==0);
        for(local i=0;i<1500 && mission.gathered<100;++i) stepSimulation(1.0/30.0);
        if(mission.gathered<100) throw "collection stalled: income="+mission.gathered+" balance="+mission.minerals+" x="+frame.units[0].x+" y="+frame.units[0].y+" cargo="+frame.units[0].cargo+" order="+frame.units[0].order+" workers="+frame.resourceNodes[0].workers;
        assert(mission.gathered>=100 && mission.minerals>=100);
        buildBarracks();
        for(local i=0;i<600 && !mission.built;++i) stepSimulation(1.0/30.0);
        assert(mission.built);
        produceMarine();
        for(local i=0;i<100;++i) stepSimulation(1.0/30.0);
        assert(mission.produced==1);
        for(local i=0;i<600 && mission.minerals<15;++i) stepSimulation(1.0/30.0);
        selectArmy();
        local manaBefore=mission.mana, mineralsBefore=mission.minerals;
        local card=hand.getCard(0);
        playNamed("grenade",29.0,17.0); // Range rejection preserves hand/account/cooldown.
        assert(card.getState()=="hand" && mission.mana==manaBefore && mission.minerals==mineralsBefore);
        local caster=unitById(selected[0]);
        playNamed("grenade",caster.x,caster.y);
        assert(card.getState()=="played" && mission.skillPlays==1);
        assert(mission.mana==manaBefore-10 && mission.minerals==mineralsBefore-15);
        stepSimulation(1.0/30.0);
        for(local i=0;i<900 && mission.minerals<40;++i) stepSimulation(1.0/30.0);
        playNamed("reinforcement",0.0,0.0);
        assert(mission.skillPlays==2);
        for(local i=0;i<100;++i) stepSimulation(1.0/30.0);
        assert(mission.produced==2);
        assault(); updateProjection();
        assert(frame.units[frame.units.len()-1].stance=="aggressive");
        for(local i=0;i<1200 && mission.skillPlays==2;++i) {
            playNamed("grenade",23.5,8.0);
            stepSimulation(1.0/30.0);
        }
        assert(mission.skillPlays==3);
        for(local i=0;i<3000 && mission.outcome=="running";++i) stepSimulation(1.0/30.0);
        if(mission.outcome!="victory" || mission.enemyHealth!=0.0) throw "battle stalled: outcome="+mission.outcome+" hp="+mission.enemyHealth+" marines="+mission.produced+" skills="+mission.skillPlays+" message="+message;
        resetGame();
        for(local i=0;i<10;++i) stepSimulation(1.0/30.0);
        assert(mission.tick==10 && mission.produced==0 && hand.count()==4);
        result <- "ok";
    )"));
    REQUIRE_EQ(vm.find("result").toString(), std::string("ok"));
}

}  // namespace test_card_rts_full_match_benchmark
