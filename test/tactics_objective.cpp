#include "common/ECS.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId logicalId(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

}  // namespace

TEST_CASE("tactics.eliminateObjectiveEndsBattleAfterDefeatOutcome") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000b0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto blueSubject = subject("00000000-0000-0000-0000-0000000000b1");
    const auto redSubject = subject("00000000-0000-0000-0000-0000000000b2");
    const auto blueUnit = subject("00000000-0000-0000-0000-0000000000b3");
    const auto redUnit = subject("00000000-0000-0000-0000-0000000000b4");
    auto blueSide = tactics.newSide(battle, blueSubject);
    auto redSide = tactics.newSide(battle, redSubject);
    REQUIRE(blueSide.ok());
    REQUIRE(redSide.ok());
    REQUIRE(tactics.newUnit(battle, blueSide.value(), blueUnit, {}, {0, 0, 0}, {1, 100, 0, 10}).ok());
    REQUIRE(tactics.newUnit(battle, redSide.value(), redUnit, {}, {1, 0, 0}, {1, 100, 0, 5}).ok());
    eve::tactics::ObjectiveSpec objective;
    objective.id = logicalId("test:eliminate-red");
    objective.kind = eve::tactics::ObjectiveKind::EliminateSide;
    objective.beneficiarySide = blueSubject;
    objective.targetSide = redSubject;
    REQUIRE(tactics.addObjective(battle, objective).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.defeatUnit(battle, redUnit).ok());

    auto status = tactics.status(battle);
    REQUIRE(status.ok());
    CHECK_EQ(static_cast<int>(status.value()), static_cast<int>(eve::tactics::BattleStatus::Ended));
    auto* value = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(value != nullptr);
    REQUIRE_EQ(value->objectives()->values.size(), 1u);
    CHECK_EQ(static_cast<int>(value->objectives()->values.front().status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
}

TEST_CASE("tactics.occupyAndSurviveObjectivesUseBoardAndRoundAuthority") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-0000000000c0"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    const auto sideSubject = subject("00000000-0000-0000-0000-0000000000c1");
    const auto unitSubject = subject("00000000-0000-0000-0000-0000000000c2");
    auto side = tactics.newSide(battle, sideSubject);
    REQUIRE(side.ok());
    REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {1, 100, 0, 10}).ok());
    eve::tactics::ObjectiveSpec occupy;
    occupy.id = logicalId("test:occupy");
    occupy.kind = eve::tactics::ObjectiveKind::OccupyCells;
    occupy.beneficiarySide = sideSubject;
    occupy.requiredCells = {{1, 0, 0}};
    occupy.endsBattle = false;
    REQUIRE(tactics.addObjective(battle, occupy).ok());
    eve::tactics::ObjectiveSpec survive;
    survive.id = logicalId("test:survive");
    survive.kind = eve::tactics::ObjectiveKind::SurviveRounds;
    survive.beneficiarySide = sideSubject;
    survive.requiredRound = 2;
    REQUIRE(tactics.addObjective(battle, survive).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, step(1)).ok());
    REQUIRE(tactics.advance(battle, step(2)).ok());
    REQUIRE(tactics.advance(battle, step(3)).ok());
    REQUIRE(tactics.moveUnit(battle, unitSubject, {1, 0, 0}).ok());
    auto* value = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(value != nullptr);
    CHECK_EQ(static_cast<int>(value->objectives()->values[0].status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
    REQUIRE(tactics.waitUnit(battle, unitSubject).ok());
    REQUIRE(tactics.advance(battle, step(4)).ok());
    REQUIRE(tactics.advance(battle, step(5)).ok());
    CHECK_EQ(static_cast<int>(value->objectives()->values[1].status),
             static_cast<int>(eve::tactics::ObjectiveStatus::Completed));
    CHECK_EQ(static_cast<int>(value->turn()->status), static_cast<int>(eve::tactics::BattleStatus::Ended));
}
