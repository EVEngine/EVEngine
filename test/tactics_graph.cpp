#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "hexmap/HexSphereTopology.h"
#include "tactics/LineOfSight.h"
#include "tactics/TacticsPath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("tactics.graphSpherePentagonsUseRealEdges") {
    auto sphere = eve::hexmap::HexSphereTopology::build(1);
    REQUIRE(sphere.ok());
    eve::tactics::BoardState board;
    board.setTopology(eve::tactics::BoardTopology::ExplicitGraph);
    for (int i = 0; i < sphere.value().cellCount(); ++i) REQUIRE(board.addCell({i, 0, 0}, {1}).ok());
    for (int i = 0; i < sphere.value().cellCount(); ++i)
        for (int d = 0; d < sphere.value().neighborCount(i); ++d)
            REQUIRE(board.addEdge({i, 0, 0}, {sphere.value().neighbor(i, d), 0, 0}).ok());
    const auto id = eve::PersistentId::parse("00000000-0000-0000-0000-000000009101");
    REQUIRE(id.has_value());
    const auto actor = eve::SubjectRef::fromPersistentId(*id);
    REQUIRE(board.place(actor, {0, 0, 0}).ok());
    auto reached = eve::tactics::PathQuery::reachable(board, actor, 1);
    REQUIRE(reached.ok());
    CHECK_EQ(board.neighbours({0, 0, 0}).size(), 5u);
    CHECK_EQ(reached.value().cells().size(), 6u);
    for (const auto& cell : reached.value().cells())
        CHECK((cell.cell.x == 0 || board.tryEdge({0, 0, 0}, cell.cell).has_value()));
    CHECK(!eve::tactics::GridLineOfSightPolicy().visible(board, {0, 0, 0}, {1, 0, 0}).ok());
    CHECK(!eve::tactics::PathQuery::cellsInRange(board, {0, 0, 0}, 0, 2, eve::tactics::CellRangeMetric::Hex).ok());
}

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
