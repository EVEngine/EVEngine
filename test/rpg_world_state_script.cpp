#include "common/Module.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("rpg.script.worldStateAcceptsStringIdentifiers") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        rpg <- eve.RPG();
        state <- rpg.newGameState();
        world <- rpg.newWorldState(state);
        before <- world.isObjectConsumed("village", "chest");
        consumed <- world.consumeObject("village", "chest");
        after <- world.isObjectConsumed("village", "chest");
        other <- world.isObjectConsumed("forest", "chest");
        reset <- world.resetObject("village", "chest");
        restored <- world.isObjectConsumed("village", "chest");
    )"));
    CHECK(!vm.find("before").toBool());
    CHECK(vm.find("consumed").toTable().find("ok").toBool());
    CHECK(vm.find("after").toBool());
    CHECK(!vm.find("other").toBool());
    CHECK(vm.find("reset").toTable().find("ok").toBool());
    CHECK(!vm.find("restored").toBool());
}
