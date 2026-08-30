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

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

}  // namespace

TEST_CASE("tactics.facadeSetupIsAtomicAndHandleOwned") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    const auto battleSubject = subject("00000000-0000-0000-0000-000000000010");
    const auto sideSubject   = subject("00000000-0000-0000-0000-000000000011");
    const auto unitSubject   = subject("00000000-0000-0000-0000-000000000012");
    const auto otherSubject  = subject("00000000-0000-0000-0000-000000000013");

    auto battleResult = tactics.newBattle(battleSubject, 42);
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, sideSubject);
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();

    auto unit = tactics.newUnit(battle, side, unitSubject, {}, {0, 0, 0});
    REQUIRE(unit.ok());
    std::move(unit).takeValue();
    auto conflict = tactics.newUnit(battle, side, otherSubject, {}, {0, 0, 0});
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Conflict);
    CHECK_EQ(tactics.unitCount(), 1u);
}

TEST_CASE("tactics.initiativePolicyAdvancesOneObservablePhase") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000020"), 7);
    REQUIRE(battleResult.ok());
    const auto battleHandle = std::move(battleResult).takeValue();
    for (int x = 0; x < 3; ++x) REQUIRE(tactics.addCell(battleHandle, {x, 0, 0}).ok());

    auto sideResult = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000021"));
    REQUIRE(sideResult.ok());
    const auto sideHandle = std::move(sideResult).takeValue();
    const auto slowSubject = subject("00000000-0000-0000-0000-000000000022");
    const auto fastSubject = subject("00000000-0000-0000-0000-000000000023");
    auto slowResult = tactics.newUnit(battleHandle, sideHandle, slowSubject, {}, {0, 0, 0}, {1, 100, 0, 10});
    auto fastResult = tactics.newUnit(battleHandle, sideHandle, fastSubject, {}, {1, 0, 0}, {1, 100, 0, 20});
    REQUIRE(slowResult.ok());
    REQUIRE(fastResult.ok());
    const auto slowHandle = std::move(slowResult).takeValue();
    const auto fastHandle = std::move(fastResult).takeValue();
    CHECK(ecs::try_get(slowHandle) != nullptr);
    CHECK(ecs::try_get(fastHandle) != nullptr);

    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    auto round = tactics.advance(battleHandle, step(1));
    REQUIRE(round.ok());
    CHECK_EQ(static_cast<int>(std::move(round).takeValue()),
             static_cast<int>(eve::tactics::BattlePhase::RoundStart));
    auto turn = tactics.advance(battleHandle, step(2));
    REQUIRE(turn.ok());
    CHECK_EQ(static_cast<int>(std::move(turn).takeValue()),
             static_cast<int>(eve::tactics::BattlePhase::TurnStart));
    auto acting = tactics.advance(battleHandle, step(3));
    REQUIRE(acting.ok());
    CHECK_EQ(static_cast<int>(std::move(acting).takeValue()), static_cast<int>(eve::tactics::BattlePhase::Acting));
    auto active = tactics.activeUnit(battleHandle);
    REQUIRE(active.ok());
    CHECK_EQ(std::move(active).takeValue(), fastSubject);

    auto moved = tactics.moveUnit(battleHandle, fastSubject, {2, 0, 0});
    REQUIRE(moved.ok());
    const auto receipt = std::move(moved).takeValue();
    CHECK_EQ(receipt.cost, 100);
    CHECK_EQ(receipt.remainingMovePoints, 0);
    REQUIRE_EQ(receipt.path.size(), 2u);
    auto moveAgain = tactics.moveUnit(battleHandle, fastSubject, {1, 0, 0});
    CHECK(!moveAgain.ok());

    REQUIRE(tactics.endTurn(battleHandle, fastSubject).ok());
    auto nextTurn = tactics.advance(battleHandle, step(4));
    REQUIRE(nextTurn.ok());
    CHECK_EQ(static_cast<int>(std::move(nextTurn).takeValue()),
             static_cast<int>(eve::tactics::BattlePhase::TurnStart));
    REQUIRE(tactics.advance(battleHandle, step(5)).ok());
    auto nextActive = tactics.activeUnit(battleHandle);
    REQUIRE(nextActive.ok());
    CHECK_EQ(std::move(nextActive).takeValue(), slowSubject);

    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    REQUIRE(battle != nullptr);
    const auto beforeFacing = battle->turn()->revision;
    REQUIRE(tactics.faceUnit(battleHandle, slowSubject, 3).ok());
    CHECK(battle->turn()->revision > beforeFacing);
    auto noOpFacing = tactics.faceUnit(battleHandle, slowSubject, 3);
    REQUIRE(noOpFacing.ok());
    CHECK_EQ(noOpFacing.code(), eve::StatusCode::NoOp);
    REQUIRE(tactics.waitUnit(battleHandle, slowSubject).ok());
    CHECK_EQ(static_cast<int>(battle->turn()->phase),
             static_cast<int>(eve::tactics::BattlePhase::TurnEnd));

    auto repeatedTick = tactics.advance(battleHandle, step(5));
    CHECK(!repeatedTick.ok());
    CHECK_EQ(repeatedTick.code(), eve::StatusCode::Rejected);
}
