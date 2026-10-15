#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "hexmap/HexSphereTopology.h"
#include "tactics/LineOfSight.h"
#include "tactics/TacticsPath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"


TEST_CASE("tactics.graphScriptMovementSnapshotAndStaleRelease") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function value(r) { if (!r.ok) throw r.status.summary; return ("value" in r) ? r.value : null; }
        local battle=value(eve.Tactics().newBattle("00000000-0000-0000-0000-000000009000",7));
        value(battle.setTopology("graph"));
        value(battle.addCell(4,0,0,1)); value(battle.addCell(99,0,0,1)); value(battle.addCell(5,0,0,1));
        value(battle.addEdge(4,0,0,99,0,0,"{}"));
        local side="00000000-0000-0000-0000-000000009201";
        local actor="00000000-0000-0000-0000-000000009101";
        value(battle.addSide(side)); value(battle.addUnit(actor,side,"test:unit",4,0,0,1,4,0,10));
        value(battle.start("initiative"));
        for(local i=1;i<=3;i++) value(battle.advance(i,1));
        local snapshot=value(battle.snapshotJson());
        if(battle.move(actor,5,0,0).ok) throw "invented grid adjacency";
        value(battle.move(actor,99,0,0));
        if(battle.restoreJson("{}").ok || value(battle.unitCell(actor)).x!=99) throw "failed restore mutated board";
        if(battle.move(actor,4,0,0).ok) throw "invented reverse edge";
        value(battle.restoreJson(snapshot));
        if(value(battle.unitCell(actor)).x!=4) throw "graph restore failed";
        value(battle.move(actor,99,0,0));
        value(battle.release());
        if(!battle.isStale()) throw "release failed";
    )"));
}
