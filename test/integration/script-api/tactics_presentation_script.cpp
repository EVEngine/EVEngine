#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "tactics/LineOfSight.h"
#include "tactics/Presentation.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief One side with two units on a straight corridor, ready to produce events. */
struct Fixture {
    ecs::Table             world;
    ecs::ScopedTable       guard{world};
    eve::tactics::Tactics  tactics;
    ecs::EntityHandle      battle{};
    eve::SubjectRef        battleSubject = subject("00000000-0000-0000-0000-000000000a00");
    eve::SubjectRef        actor         = subject("00000000-0000-0000-0000-000000000a01");
    eve::SubjectRef        ally          = subject("00000000-0000-0000-0000-000000000a02");

    void build() {
        auto created = tactics.newBattle(battleSubject, 19);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        for (int x = 0; x < 4; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000a03"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), actor, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), ally, {}, {3, 0, 0}, {1, 300, 0, 5}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

/** @brief A sight policy that blocks everything, to prove the registered one is used. */
class BlindPolicy final : public eve::tactics::ILineOfSightPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "test-blind"; }
    [[nodiscard]] eve::Result<bool> visible(const eve::tactics::BoardState&, eve::tactics::Cell,
                                            eve::tactics::Cell) const override {
        return eve::Result<bool>::success(false);
    }
};

}  // namespace


TEST_CASE("tactics.scriptInteractionSessionTurnsClicksIntoIntents") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000b10", 21);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000b11";
            local allyId = "00000000-0000-0000-0000-000000000b13";
            local sideId = "00000000-0000-0000-0000-000000000b12";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local a = battle.addUnit(allyId, sideId, "test:unit", 2, 0, 0, 1, 300, 0, 5);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // `newInteraction` returns the usual Result table; its `value` is the session object.
            local ixResult = battle.newInteraction(unitId);
            local interaction = ixResult.ok ? ixResult.value : null;
            local idleResult = battle.newInteraction(allyId);
            local idle = idleResult.ok ? idleResult.value : null;
            if (interaction != null && idle != null) {
                local initial = interaction.state();               // "await_selection"
                local reachable = interaction.reachableCells();    // the projection the machine reads
                local revision = interaction.expectedRevision();

                // Selecting the controlled unit, then a reachable cell, yields an intention.
                local select = interaction.click(0, 0, 0);
                local afterSelect = interaction.state();
                local move = interaction.click(1, 0, 0);
                local afterMove = interaction.state();
                // A pending intention must be committed or dropped before the next one.
                local queued = interaction.click(2, 0, 0);
                local resolved = interaction.resolve();
                local afterResolve = interaction.state();

                // Arming an ability with script-supplied target cells.
                local armed = interaction.armAbility("test:strike", "[[2,0,0]]");
                local targeting = interaction.state();
                local declared = interaction.click(2, 0, 0);

                // A unit that is not active gets a session that exists and reports itself blocked.
                local idleState = idle.state();
                local idleClick = idle.click(0, 0, 0);

                if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && a.ok && started.ok &&
                    p1.ok && p2.ok && p3.ok &&
                    initial == "await_selection" && revision > 0 &&
                    reachable.value.len() >= 2 &&
                    select.ok && select.value.kind == "select_unit" && select.value.actor == unitId &&
                    afterSelect == "unit_selected" &&
                    move.ok && move.value.kind == "move_to" && move.value.cell.x == 1 &&
                    afterMove == "resolving" && !queued.ok &&
                    resolved.ok && afterResolve == "await_selection" &&
                    armed.ok && targeting == "targeting" &&
                    declared.ok && declared.value.kind == "use_ability_on" &&
                    declared.value.action == "test:strike" && declared.value.targetUnit == allyId &&
                    idleState == "blocked" && !idleClick.ok) {
                    result = "ok";
                } else {
                    result = "s=" + initial + "/" + afterSelect + "/" + afterMove + "/" + afterResolve + "/" +
                             targeting + "/" + idleState +
                             " f=" + (select.ok?1:0) + (move.ok?1:0) + (queued.ok?1:0) + (resolved.ok?1:0) +
                             (armed.ok?1:0) + (declared.ok?1:0) + (idleClick.ok?1:0) +
                             " n=" + reachable.value.len() + " rev=" + revision;
                }
            } else {
                result = "no interaction instance";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptReachesPresentationAndSightQueries") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000a10", 20);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000a11";
            local sideId = "00000000-0000-0000-0000-000000000a12";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // Sight-aware range, and which policy answered it.
            local visible = battle.visibleCellsInRange(0, 0, 0, 1, 2, "chebyshev");
            local algorithm = battle.lineOfSightAlgorithm();

            // Presentation intents: a move must come back as Moving with a revert contract.
            local before = battle.eventCount();
            local moved = battle.move(unitId, 1, 0, 0);
            local intents = battle.presentationIntents(before, 5);
            local sawMoving = false;
            local sawRevert = false;
            local toX = -1;
            if (intents.ok) {
                foreach (intent in intents.value) {
                    if (intent.state == "moving") {
                        sawMoving = true;
                        toX = intent.to.x;
                        if (intent.revert.trigger == "on_expiry" &&
                            intent.revert.restingState == "idle" &&
                            intent.revert.sequence == intent.sequence) sawRevert = true;
                    }
                }
            }

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok &&
                visible.ok && visible.value.len() > 0 &&
                algorithm == "grid_line_of_sight" &&
                intents.ok && sawMoving && sawRevert && toX == 1) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
