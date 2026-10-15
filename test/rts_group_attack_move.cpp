#include "RtsCompositionFixtures.h"

TEST_CASE("rts.groups.attackMoveAdmissionAndRestorePreserveOrders") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    auto                         a      = subject("00000000-0000-7000-8000-00000000a101");
    auto                         b      = subject("00000000-0000-7000-8000-00000000a102");
    auto*                        first  = rts.newUnit(a).takeValue();
    auto*                        second = rts.newUnit(b).takeValue();
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.target    = {10.f, 5.f};
    batch.formation = {FormationKind::Grid, 1.f, 2};
    batch.kind      = OrderKind::AttackMove;
    REQUIRE(rts.submitMovementGroup(batch).ok());
    REQUIRE(first->orders()->values.current().value().kind == OrderKind::AttackMove);
    auto saved = rts.snapshotState();
    REQUIRE(saved.ok());
    REQUIRE_EQ(saved.value().movementGroups.size(), 1u);
    REQUIRE(rts.restoreState(saved.value()).ok());
    REQUIRE(second->orders()->values.current().value().kind == OrderKind::AttackMove);
    batch.kind = OrderKind::Attack;
    REQUIRE(!rts.submitMovementGroup(batch).ok());
    REQUIRE_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
    REQUIRE(first->orders()->values.current().value().kind == OrderKind::AttackMove);
}

TEST_CASE("rts.groups.attackMoveReplayV6RoundTripsAndRejectsV5CombinationAtomically") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             a = subject("00000000-0000-7000-8000-00000000a201");
    auto             b = subject("00000000-0000-7000-8000-00000000a202");
    REQUIRE(rts.newUnit(a).ok());
    REQUIRE(rts.newUnit(b).ok());
    eve::rts::RTSReplayCommand command;
    command.operation      = eve::rts::RTSReplayOperation::MovementGroup;
    command.tick           = eve::SimulationTick(1);
    command.units          = {a, b};
    command.command.kind   = OrderKind::AttackMove;
    command.command.target = {10.f, 5.f};
    command.formation      = {FormationKind::Grid, 1.f, 2};
    eve::rts::RTSCommandLog log;
    REQUIRE(log.queue(command).ok());
    auto encoded = log.exportText();
    REQUIRE(encoded.starts_with("EVERTS_COMMANDS 6"));
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(encoded).ok());
    auto before = imported.exportText();
    auto legacy = encoded;
    legacy.replace(0, std::string("EVERTS_COMMANDS 6").size(), "EVERTS_COMMANDS 5");
    REQUIRE(!imported.importText(legacy).ok());
    REQUIRE_EQ(imported.exportText(), before);
    REQUIRE(imported.apply(eve::SimulationTick(1), rts).ok());
    REQUIRE_EQ(rts.snapshotState().value().movementGroups.size(), 1u);
}


TEST_CASE("rts.groups.attackMoveEngagedMemberDoesNotHoldBackAdvancingMember") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             a         = subject("00000000-0000-7000-8000-00000000a301");
    auto             b         = subject("00000000-0000-7000-8000-00000000a302");
    auto             enemyId   = subject("00000000-0000-7000-8000-00000000a303");
    auto*            engaged   = rts.newUnit(a).takeValue();
    auto*            advancing = rts.newUnit(b).takeValue();
    auto*            enemy     = rts.newUnit(enemyId).takeValue();
    engaged->motion()->x       = -10.f;
    advancing->motion()->x     = 0.f;
    enemy->motion()->x         = -9.f;
    engaged->motion()->speed = advancing->motion()->speed = 4.f;
    // This case exercises ranged engagement; melee now moves into an admitted approach slot.
    engaged->crowd()->radius = advancing->crowd()->radius = enemy->crowd()->radius = 0.35f;
    engaged->combat()->target                                                      = ecs::handle_of(enemy);
    engaged->combat()->engagementRange                                             = 2.f;
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.kind      = OrderKind::AttackMove;
    batch.target    = {10.f, 0.f};
    batch.formation = {FormationKind::Line, 1.f, 0};
    REQUIRE(rts.submitMovementGroup(batch).ok());
    PendingRTSExecutor executor;
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromSeconds(0.05).takeValue()}, executor).ok());
    CHECK_EQ(advancing->navigation()->formationSpeedFactor, 1.f);
    CHECK(advancing->motion()->x > 0.19f);
    CHECK_EQ(engaged->motion()->x, -10.f);
}


TEST_CASE("rts.groups.attackMoveArrivalRetiresMovingGroupAndKeepsCombatStance") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    const auto                   a      = subject("00000000-0000-7000-8000-00000000a401");
    const auto                   b      = subject("00000000-0000-7000-8000-00000000a402");
    auto*                        first  = rts.newUnit(a).takeValue();
    auto*                        second = rts.newUnit(b).takeValue();
    eve::rts::MovementGroupBatch batch;
    batch.units     = {a, b};
    batch.kind      = OrderKind::AttackMove;
    batch.target    = {10.f, 0.f};
    batch.formation = {FormationKind::Line, 1.f, 0};
    REQUIRE(rts.submitMovementGroup(batch).ok());
    for (auto* unit : {first, second}) {
        const auto target       = unit->orders()->values.current().value().target;
        unit->motion()->x       = target.x;
        unit->motion()->y       = target.y;
        unit->motion()->arrived = true;
    }
    PendingRTSExecutor executor;
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromSeconds(0.05).takeValue()}, executor).ok());
    REQUIRE(rts.snapshotState().value().movementGroups.empty());
    CHECK(first->orders()->values.current().value().kind == OrderKind::AttackMove);
    CHECK(second->orders()->values.current().value().kind == OrderKind::AttackMove);
}
