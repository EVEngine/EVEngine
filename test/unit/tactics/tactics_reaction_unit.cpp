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

}  // namespace

TEST_CASE("tactics.reactionConsumesResourceAndPreventsSameTriggerCycle") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;

    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000060"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000061"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    const auto actor = subject("00000000-0000-0000-0000-000000000062");
    const auto defender = subject("00000000-0000-0000-0000-000000000063");
    REQUIRE(tactics.newUnit(battle, side, actor, {}, {0, 0, 0}, {1, 0, 0, 20}).ok());
    REQUIRE(tactics.newUnit(battle, side, defender, {}, {1, 0, 0}, {1, 0, 1, 10}).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());

    const auto reaction = *eve::LogicalId::parse("tactics:counter");
    auto opened = tactics.openReaction(battle, 4, {{defender, reaction, 5, 10}});
    REQUIRE(opened.ok());
    CHECK_EQ(std::move(opened).takeValue(), 1u);
    auto accepted = tactics.acceptReaction(battle, defender, reaction);
    REQUIRE(accepted.ok());
    CHECK_EQ(std::move(accepted).takeValue().remainingReactionPoints, 0);
    auto* battleValue = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(battleValue != nullptr);
    const auto& acceptedEvent = battleValue->events()->values.back();
    CHECK(acceptedEvent.causationCommand != 0);
    CHECK(acceptedEvent.correlationCommand != 0);
    CHECK(acceptedEvent.correlationCommand != acceptedEvent.causationCommand);

    REQUIRE(tactics.openReaction(battle, 4, {{defender, reaction, 5, 10}}).ok());
    auto duplicate = tactics.acceptReaction(battle, defender, reaction);
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    REQUIRE(tactics.declineReaction(battle).ok());
}

TEST_CASE("tactics.reactionDepthIsBounded") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::tactics::Tactics tactics;
    auto battleResult = tactics.newBattle(subject("00000000-0000-0000-0000-000000000070"));
    REQUIRE(battleResult.ok());
    const auto battle = std::move(battleResult).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    auto sideResult = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000071"));
    REQUIRE(sideResult.ok());
    const auto side = std::move(sideResult).takeValue();
    const auto actor = subject("00000000-0000-0000-0000-000000000072");
    REQUIRE(tactics.newUnit(battle, side, actor, {}, {0, 0, 0}, {1, 0, 8, 1}).ok());
    REQUIRE(tactics.start(battle, eve::tactics::TurnPolicyKind::Initiative).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(1), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(2), eve::Duration::zero()}).ok());
    REQUIRE(tactics.advance(battle, {eve::SimulationTick(3), eve::Duration::zero()}).ok());
    const auto reaction = *eve::LogicalId::parse("tactics:counter");
    auto* battleValue = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battle));
    REQUIRE(battleValue != nullptr);
    for (std::uint64_t depth = 1; depth <= 8; ++depth) {
        const auto trigger = battleValue->events()->nextSequence - 1;
        REQUIRE(tactics.openReaction(battle, trigger, {{actor, reaction, 0, 1}}).ok());
    }
    const auto overflowTrigger = battleValue->events()->nextSequence - 1;
    auto overflow = tactics.openReaction(battle, overflowTrigger, {{actor, reaction, 0, 1}});
    CHECK(!overflow.ok());
    CHECK_EQ(overflow.code(), eve::StatusCode::Rejected);
}
