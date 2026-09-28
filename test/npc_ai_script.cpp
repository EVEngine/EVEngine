#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "npc_ai/NpcAiModule.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("npcAiScript.worldOwnsSignalAndBlackboardAgents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::npc_ai::NpcAi::expose(eve);
    vm.run(vm.compileSource(R"(
        npc <- eve.NpcAi();
        created <- npc.newWorld(256, 32);
        world <- created.value;
        behaviorJson <- "{\"id\":\"guard\",\"schemaVersion\":1,\"initialState\":\"idle\",\"blackboardSchema\":[{\"key\":\"enemy\",\"type\":\"String\",\"required\":false}],\"states\":[{\"id\":\"idle\",\"transitions\":[{\"targetState\":\"alert\",\"signal\":\"enemy_seen\",\"priority\":10}]},{\"id\":\"alert\",\"enterConditions\":[{\"key\":\"enemy\",\"op\":\"Exists\"}]}]}";
        registered <- world.registerBehavior(behaviorJson);
        validated <- world.validateBehavior(behaviorJson);
        agentResult <- world.createAgent("guard");
        agent <- agentResult.value;
        wrote <- world.setBlackboardString(agent, "enemy", "player");
        first <- world.tick(1, 0.016, 8, 8);
        signaled <- world.signal(agent, "enemy_seen");
        second <- world.tick(2, 0.016, 8, 8);
        snap <- world.snapshot(agent);
        remembered <- world.remember(agent, "player", "sight", 0.9, 2, 30, "{}");
        destroyed <- world.destroyAgent(agent);
        stale <- world.isAgentStale(agent);
        afterDestroy <- world.snapshot(agent);
        activeState <- snap.value.activeState;
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("world").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("registered").toTable().get<bool>("ok"));
    CHECK(vm.find("validated").toTable().get<bool>("ok"));
    CHECK(vm.find("agentResult").toTable().get<bool>("ok"));
    CHECK(vm.find("wrote").toTable().get<bool>("ok"));
    CHECK(vm.find("first").toTable().get<bool>("ok"));
    CHECK(vm.find("signaled").toTable().get<bool>("ok"));
    CHECK(vm.find("second").toTable().get<bool>("ok"));
    CHECK(vm.find("snap").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("activeState").toString(), std::string("alert"));
    CHECK(vm.find("remembered").toTable().get<bool>("ok"));
    CHECK(vm.find("destroyed").toTable().get<bool>("ok"));
    CHECK(vm.find("stale").toBool());
    CHECK(!vm.find("afterDestroy").toTable().get<bool>("ok"));
}
