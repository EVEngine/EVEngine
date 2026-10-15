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

TEST_CASE("tactics.presentationIntentsFollowTheEventLogAndCarryCommandGeometry") {
    Fixture fixture;
    fixture.build();

    // Turn boundaries are already in the log from `build`: the pending turn selects the actor.
    auto opening = fixture.tactics.presentationIntents(fixture.battle, 0, 5);
    REQUIRE(opening.ok());
    REQUIRE(!opening.value().empty());
    const auto selected = std::find_if(opening.value().begin(), opening.value().end(),
                                       [](const eve::tactics::PresentationCommand& command) {
                                           return command.intent.state == eve::tactics::UnitVisualState::Selected;
                                       });
    REQUIRE(selected != opening.value().end());
    CHECK_EQ(selected->intent.subject, fixture.actor);
    // Selection is cleared by the next turn, not by a timer, and returns to the unit's resting
    // state: these are the two facts a view-object API cannot state.
    CHECK(selected->revert.trigger == eve::tactics::PresentationRevertTrigger::OnNextTurn);
    CHECK(selected->revert.restingState == eve::tactics::UnitVisualState::Idle);

    // A move produces a Moving intent whose destination comes from the accepted command, which
    // the event points at. The event itself carries no geometry.
    const std::uint64_t beforeMove = selected->intent.sequence;
    REQUIRE(fixture.tactics.moveUnit(fixture.battle, fixture.actor, {1, 0, 0}).ok());
    auto moved = fixture.tactics.presentationIntents(fixture.battle, beforeMove, 5);
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value().size(), std::size_t{1});
    CHECK(moved.value()[0].intent.state == eve::tactics::UnitVisualState::Moving);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(moved.value()[0].intent.to == destination);
    CHECK_EQ(moved.value()[0].intent.expiresAtTick.value(), std::uint64_t{3 + 5});

    // An ability declaration names its target through the command as well.
    REQUIRE(fixture.tactics.useAbility(fixture.battle, fixture.actor, *eve::LogicalId::parse("test:strike"),
                                       {3, 0, 0}, fixture.ally, "{\"power\":1}")
                .ok());
    auto declared = fixture.tactics.presentationIntents(fixture.battle, moved.value()[0].intent.sequence, 5);
    REQUIRE(declared.ok());
    REQUIRE_EQ(declared.value().size(), std::size_t{1});
    CHECK(declared.value()[0].intent.state == eve::tactics::UnitVisualState::Attacking);
    CHECK_EQ(declared.value()[0].intent.other, fixture.ally);

    // Finishing the activation makes "finished" the resting state, so a later transient intent
    // for that unit reverts to Finished rather than to Idle.
    REQUIRE(fixture.tactics.endTurn(fixture.battle, fixture.actor).ok());
    auto completed = fixture.tactics.presentationIntents(fixture.battle, declared.value()[0].intent.sequence, 5);
    REQUIRE(completed.ok());
    REQUIRE_EQ(completed.value().size(), std::size_t{1});
    CHECK(completed.value()[0].intent.state == eve::tactics::UnitVisualState::Finished);
    CHECK(completed.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnRoundStart);

    // A defeat is durable: no trigger, no deadline, and asking to revert it is refused.
    REQUIRE(fixture.tactics.defeatUnit(fixture.battle, fixture.ally).ok());
    auto defeated = fixture.tactics.presentationIntents(fixture.battle, completed.value()[0].intent.sequence, 5);
    REQUIRE(defeated.ok());
    REQUIRE_EQ(defeated.value().size(), std::size_t{1});
    CHECK(defeated.value()[0].intent.state == eve::tactics::UnitVisualState::Destroyed);
    CHECK(!defeated.value()[0].revert.isRevertable());
    CHECK_EQ(defeated.value()[0].intent.expiresAtTick.value(), std::uint64_t{0});
}

TEST_CASE("tactics.sightAwareRangeUsesTheRegisteredPolicyAndReportsWhichOne") {
    Fixture fixture;
    fixture.build();

    // Default: the built-in grid rule, reported by id so a caller never has to assume it.
    CHECK_EQ(fixture.tactics.lineOfSightAlgorithm(), std::string_view("grid_line_of_sight"));
    auto open = fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3,
                                                   eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(open.ok());
    CHECK(!open.value().empty());

    // Registered: a project board replaces the rule, and the reported id changes with it.
    BlindPolicy blind;
    eve::cap::provide<eve::tactics::ILineOfSightPolicy>(&blind);
    CHECK_EQ(fixture.tactics.lineOfSightAlgorithm(), std::string_view("test-blind"));
    auto blinded = fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3,
                                                      eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(blinded.ok());
    CHECK(blinded.value().empty());

    // Revoking returns to the built-in rule instead of leaving the facade pointing at a dead
    // provider.
    eve::cap::revoke<eve::tactics::ILineOfSightPolicy>(&blind);
    auto again = fixture.tactics.visibleCellsInRange(fixture.battle, {0, 0, 0}, 1, 3,
                                                    eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(again.ok());
    CHECK(again.value() == open.value());
}
