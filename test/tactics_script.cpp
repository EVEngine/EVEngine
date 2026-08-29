#include "common/ECS.h"
#include "common/Module.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("tactics.scriptOwnedBattleRunsDeterministicTurn") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-0000000000d0", 42);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide("00000000-0000-0000-0000-0000000000d1");
            local unit = battle.addUnit("00000000-0000-0000-0000-0000000000d2",
                                       "00000000-0000-0000-0000-0000000000d1",
                                       "test:unit", 0, 0, 0, 1, 100, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            local moved = battle.move("00000000-0000-0000-0000-0000000000d2", 1, 0, 0);
            local rolled = battle.roll("combat:hit");
            local event = battle.eventAt(battle.eventCount() - 1);
            if (c0.ok && c1.ok && side.ok && unit.ok && started.ok && p1.ok && p2.ok && p3.ok && moved.ok && rolled.ok &&
                battle.status().value == "running" && battle.phase().value == "acting" &&
                moved.value.cost == 100 && rolled.value.len() > 0 && event.ok && event.value.type == "random.rolled" &&
                battle.ownership() == "owned" && !battle.isStale()) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptReleaseMakesProxyStale") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local created = eve.Tactics().newBattle("00000000-0000-0000-0000-0000000000e0", 1);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local released = battle.release();
            if (released.ok && battle.isStale() && !battle.addCell(0, 0, 0, 100).ok) result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
