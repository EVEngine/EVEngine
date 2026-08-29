#include "action/Action.h"
#include "common/ECS.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsAction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.moveCommitsThroughSharedActionRuntime") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000040"), 9);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000041"));
    REQUIRE(sideResult.ok());
    const auto sideHandle = std::move(sideResult).takeValue();
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000042");
    auto unitResult = tactics.newUnit(battleHandle, sideHandle, unitSubject, {}, {0, 0, 0}, {1, 100, 0, 1});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();

    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices      services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    auto request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    const auto execution = std::move(submitted).takeValue();
    auto advanced = runtime.advance(execution, eve::SimulationTick(4), eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(std::move(advanced).takeValue().phase),
             static_cast<int>(eve::action::ActionPhase::Completed));

    const auto position = battle->board()->value.position(unitSubject);
    REQUIRE(position.has_value());
    CHECK((*position == eve::tactics::Cell{1, 0, 0}));
    auto* unit = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(unit != nullptr);
    CHECK_EQ(unit->turn()->movePoints, 0);

    auto faceRequest = eve::tactics::makeFaceRequest(unitHandle, 2, eve::SimulationTick(4));
    REQUIRE(faceRequest.ok());
    auto faceExecution = runtime.submit(eve::tactics::faceActionDefinition(), std::move(faceRequest).takeValue());
    REQUIRE(faceExecution.ok());
    auto faceAdvanced =
        runtime.advance(std::move(faceExecution).takeValue(), eve::SimulationTick(5), eve::Duration::zero());
    REQUIRE(faceAdvanced.ok());
    CHECK_EQ(unit->position()->facing, 2);

    auto waitRequest = eve::tactics::makeWaitRequest(unitHandle, eve::SimulationTick(5));
    REQUIRE(waitRequest.ok());
    auto waitExecution = runtime.submit(eve::tactics::waitActionDefinition(), std::move(waitRequest).takeValue());
    REQUIRE(waitExecution.ok());
    auto waitAdvanced =
        runtime.advance(std::move(waitExecution).takeValue(), eve::SimulationTick(6), eve::Duration::zero());
    REQUIRE(waitAdvanced.ok());
    CHECK_EQ(static_cast<int>(battle->turn()->phase), static_cast<int>(eve::tactics::BattlePhase::TurnEnd));
}

TEST_CASE("tactics.moveActionPrepareFailureDoesNotMutateBoard") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000050"));
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000051"));
    REQUIRE(sideResult.ok());
    const auto sideHandle = std::move(sideResult).takeValue();
    const auto actor = subject("00000000-0000-0000-0000-000000000052");
    auto unitResult = tactics.newUnit(battleHandle, sideHandle, actor, {}, {0, 0, 0}, {1, 0, 0, 1});
    REQUIRE(unitResult.ok());
    const auto unitHandle = std::move(unitResult).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices      services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    const auto beforeRevision = battle->turn()->revision;
    const auto beforeEvents = battle->events()->values.size();
    const auto beforeCommands = battle->commands()->values.size();
    const auto beforeStreams = battle->random()->streams.size();
    auto request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    auto advanced = runtime.advance(std::move(submitted).takeValue(), eve::SimulationTick(4), eve::Duration::zero());
    CHECK(!advanced.ok());
    const auto position = battle->board()->value.position(actor);
    REQUIRE(position.has_value());
    CHECK((*position == eve::tactics::Cell{0, 0, 0}));
    CHECK_EQ(battle->turn()->revision, beforeRevision);
    CHECK_EQ(battle->events()->values.size(), beforeEvents);
    CHECK_EQ(battle->commands()->values.size(), beforeCommands);
    CHECK_EQ(battle->random()->streams.size(), beforeStreams);
}

TEST_CASE("tactics.moveActionRejectsStaleExpectedRevision") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000055"));
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto side = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000056"));
    REQUIRE(side.ok());
    const auto actor = subject("00000000-0000-0000-0000-000000000057");
    auto unit = tactics.newUnit(battleHandle, side.value(), actor, {}, {0, 0, 0}, {1, 100, 0, 1});
    REQUIRE(unit.ok());
    const auto unitHandle = std::move(unit).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());
    auto request = eve::tactics::makeMoveRequest(unitHandle, {1, 0, 0}, eve::SimulationTick(3));
    REQUIRE(request.ok());
    REQUIRE(tactics.faceUnit(battleHandle, actor, 1).ok());

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    eve::tactics::MoveActionExecutor executor(*battle);
    eve::action::ActionServices services;
    services.effects = &executor;
    eve::action::ActionRuntime runtime(services);
    auto submitted = runtime.submit(eve::tactics::moveActionDefinition(), std::move(request).takeValue());
    REQUIRE(submitted.ok());
    auto advanced = runtime.advance(std::move(submitted).takeValue(), eve::SimulationTick(4), eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(advanced.error()->code(), eve::DiagnosticCode::Conflict);
    CHECK_EQ(battle->board()->value.position(actor).value(), (eve::tactics::Cell{0, 0, 0}));
}
