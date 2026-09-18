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

TEST_CASE("tactics.scriptReactionWindowOpensAcceptsAndDeclines") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000100", 3);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local actorId = "00000000-0000-0000-0000-000000000101";
            local defenderId = "00000000-0000-0000-0000-000000000102";
            local sideId = "00000000-0000-0000-0000-000000000103";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            // defender carries one reaction point, actor carries none
            local u0 = battle.addUnit(actorId, sideId, "test:unit", 0, 0, 0, 1, 0, 0, 20);
            local u1 = battle.addUnit(defenderId, sideId, "test:unit", 1, 0, 0, 1, 0, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // A malformed candidate array must be refused with a path, not crash.
            local badJson = battle.openReaction(4, "[{\"reactor\":\"not-a-uuid\",\"action\":\"tactics:counter\"}]");
            // An empty candidate list is refused.
            local emptyJson = battle.openReaction(4, "[]");

            local opened = battle.openReaction(4,
                "[{\"reactor\":\"" + defenderId + "\",\"action\":\"tactics:counter\"," +
                "\"priority\":5,\"initiative\":10}]");
            local accepted = battle.acceptReaction(defenderId, "tactics:counter");
            // The window is closed now, so there is nothing left to decline.
            local declined = battle.declineReaction();

            if (c0.ok && c1.ok && side.ok && u0.ok && u1.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                !badJson.ok && !emptyJson.ok &&
                opened.ok && opened.value == 1 &&
                accepted.ok && accepted.value.remainingReactionPoints == 0 &&
                !declined.ok) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptEndTurnAndFinishAreReachableWithoutWait") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-0000000000f0", 7);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local c0 = battle.addCell(0, 0, 0, 100);
            local side = battle.addSide("00000000-0000-0000-0000-0000000000f1");
            local unit = battle.addUnit("00000000-0000-0000-0000-0000000000f2",
                                        "00000000-0000-0000-0000-0000000000f1",
                                        "test:unit", 0, 0, 0, 1, 100, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            // endTurn takes the battle out of Acting without forcing a wait action.
            local ended = battle.endTurn("00000000-0000-0000-0000-0000000000f2");
            local phaseAfterEndTurn = battle.phase().value;
            local turnEvent = battle.eventAt(battle.eventCount() - 1);
            // finish closes a still-running battle from any phase.
            local finished = battle.finish();
            local statusAfterFinish = battle.status().value;
            local phaseAfterFinish = battle.phase().value;
            local battleEvent = battle.eventAt(battle.eventCount() - 1);
            // A second finish must be refused, not silently accepted.
            local secondFinish = battle.finish();
            if (c0.ok && side.ok && unit.ok && started.ok && p1.ok && p2.ok && p3.ok &&
                ended.ok && finished.ok && !secondFinish.ok &&
                phaseAfterEndTurn == "turn_end" && statusAfterFinish == "ended" &&
                phaseAfterFinish == "battle_end" && turnEvent.value.type == "turn.completed" &&
                battleEvent.value.type == "battle.ended") {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptPreviewReachabilityAndResourcesMatchCommit") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000110", 5);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000111";
            local sideId = "00000000-0000-0000-0000-000000000112";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            // actionPoints 1, movePoints 300, reactionPoints 0, initiative 10
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            local before = battle.unitResources(unitId);
            local reach = battle.reachable(unitId, 300);
            // unknown metric must be refused, not silently defaulted
            local badMetric = battle.cellsInRange(0, 0, 0, 1, 2, "nope");
            // manhattan distances from (0,0): 0,1,2 -> [1,2] keeps two cells
            local range = battle.cellsInRange(0, 0, 0, 1, 2, "manhattan");
            // preview of the full-length move: two steps of cost 100
            local preview = battle.previewMove(unitId, 2, 0, 0);
            local previewWait = battle.previewWait(unitId);

            local moved = battle.move(unitId, 1, 0, 0);
            // after committing one step, the remaining cost to (2,0) is one step
            local afterPreview = battle.previewMove(unitId, 2, 0, 0);
            local after = battle.unitResources(unitId);

            local ended = battle.endTurn(unitId);
            local afterEnd = battle.unitResources(unitId);

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                before.ok && before.value.movePoints == 300 && before.value.acted == false &&
                reach.ok && reach.value.cells.len() == 3 &&
                !badMetric.ok &&
                range.ok && range.value.len() == 2 &&
                preview.ok && preview.value.cost == 200 && previewWait.ok &&
                moved.ok && afterPreview.ok && afterPreview.value.cost == 100 &&
                after.value.movePoints == 200 &&
                ended.ok && afterEnd.value.acted == true) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptEdgesAreDirectedAndAffectReachability") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000120", 9);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000121";
            local sideId = "00000000-0000-0000-0000-000000000122";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 1000, 0, 10);

            // Malformed spec shapes are refused before they reach the board.
            local badBool = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"passable\":1}");
            local badTags = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"tags\":[1]}");
            // (2,0) is two steps away, so it is not adjacent to (0,0).
            local notAdjacent = battle.addEdge(0, 0, 0, 2, 0, 0, "");

            local declared = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"passable\":false,\"tags\":[\"door\"]}");
            local duplicate = battle.addEdge(0, 0, 0, 1, 0, 0, "");
            local forward = battle.edge(0, 0, 0, 1, 0, 0);
            local forwardPresent = battle.hasEdge(0, 0, 0, 1, 0, 0);
            local reversePresent = battle.hasEdge(1, 0, 0, 0, 0, 0);

            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            // The only outgoing direction is blocked, so only the origin is reachable.
            local reach = battle.reachable(unitId, 1000);

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                !badBool.ok && !badTags.ok && !notAdjacent.ok &&
                declared.ok && !duplicate.ok &&
                forward.ok && forward.value.passable == false &&
                forward.value.tags.len() == 1 && forward.value.tags[0] == "door" &&
                forwardPresent && !reversePresent &&
                reach.ok && reach.value.cells.len() == 1) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptAbilityDeclarationIsCostedAndLogged") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000140", 14);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000141";
            local sideId = "00000000-0000-0000-0000-000000000142";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            // Two action points, so exactly two declarations are affordable.
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            local first = battle.useAbility(unitId, "test:strike", 1, 0, 0);
            // Refusals run while a point is still available, so they exercise the
            // validation path rather than the exhausted-resource path.
            local badLayer = battle.useAbility(unitId, "test:strike", 1, 0, 4);
            local badAction = battle.useAbility(unitId, "", 1, 0, 0);
            local second = battle.useAbility(unitId, "test:strike", 0, 0, 0);
            local exhausted = battle.useAbility(unitId, "test:strike", 1, 0, 0);

            local resources = battle.unitResources(unitId);
            local all = battle.commandsFrom(0);
            local declared = 0;
            local sawActor = false;
            local sawCell = false;
            if (all.ok) {
                foreach (command in all.value) {
                    if (command.kind == "use_ability") {
                        declared = declared + 1;
                        if (command.actor == unitId) sawActor = true;
                        if (command.cell.x == 1 && command.cell.y == 0 && command.cell.layer == 0) sawCell = true;
                    }
                }
            }

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                first.ok && second.ok && !exhausted.ok && !badLayer.ok && !badAction.ok &&
                resources.ok && resources.value.actionPoints == 0 &&
                all.ok && declared == 2 && sawActor && sawCell) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptCommandLogIsReadableAndRevisionScoped") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000130", 13);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000131";
            local sideId = "00000000-0000-0000-0000-000000000132";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            local moved = battle.move(unitId, 1, 0, 0);
            local ended = battle.endTurn(unitId);

            // Negative revision is a validation error, not an empty log.
            local negative = battle.commandsFrom(-1);
            local all = battle.commandsFrom(0);
            local future = battle.commandsFrom(999999);

            local sawStart = false;
            local sawMove = false;
            local startPolicy = "";
            if (all.ok) {
                foreach (command in all.value) {
                    if (command.kind == "start") { sawStart = true; startPolicy = command.policyId; }
                    if (command.kind == "move") sawMove = true;
                }
            }

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok && ended.ok &&
                !negative.ok &&
                all.ok && all.value.len() >= 5 && sawStart && sawMove &&
                startPolicy == "initiative" &&
                future.ok && future.value.len() == 0) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
