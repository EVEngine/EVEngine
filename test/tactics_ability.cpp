#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "tactics/Tactics.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief One side with a single acting unit, ready to declare abilities. */
struct Fixture {
    ecs::Table             world;
    ecs::ScopedTable       guard{world};
    eve::tactics::Tactics  tactics;
    ecs::EntityHandle      battle{};
    eve::SubjectRef        battleSubject = subject("00000000-0000-0000-0000-000000000500");
    eve::SubjectRef        sideSubject   = subject("00000000-0000-0000-0000-000000000501");
    eve::SubjectRef        unitSubject   = subject("00000000-0000-0000-0000-000000000502");

    /** @param actionPoints per-round action points, so exhaustion is reachable. */
    void build(int actionPoints = 2) {
        auto created = tactics.newBattle(battleSubject, 17);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, sideSubject);
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0},
                                {actionPoints, 300, 0, 10})
                    .ok());
    }

    /** @brief Start and step to Acting, which is the only phase that accepts an ability. */
    void enterActing() {
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
        auto phase = tactics.phase(battle);
        REQUIRE(phase.ok());
        REQUIRE(phase.value() == eve::tactics::BattlePhase::Acting);
    }

    [[nodiscard]] int actionPoints() {
        auto resources = tactics.unitResources(battle, unitSubject);
        REQUIRE(resources.ok());
        return resources.value().actionPoints;
    }

    [[nodiscard]] std::vector<eve::tactics::BattleCommand> commands() {
        auto commands = tactics.commandsFrom(battle, eve::Revision(0));
        REQUIRE(commands.ok());
        return std::move(commands).takeValue();
    }
};

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

}  // namespace

TEST_CASE("tactics.abilitySpendsAnActionPointAndRecordsTheDeclaration") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();
    REQUIRE_EQ(fixture.actionPoints(), 2);

    auto declared = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                {1, 0, 0});
    REQUIRE(declared.ok());
    // Applied, not NoOp: an ability that silently did nothing would still cost a point.
    CHECK_EQ(declared.code(), eve::StatusCode::Applied);
    CHECK_EQ(fixture.actionPoints(), 1);

    const auto commands = fixture.commands();
    REQUIRE(!commands.empty());
    const auto& last = commands.back();
    CHECK(last.kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(last.actor, fixture.unitSubject);
    CHECK_EQ(last.action.format(), std::string("test:strike"));
    const eve::tactics::Cell expectedTarget{1, 0, 0};
    CHECK(last.cell == expectedTarget);
    // A declaration is not a reaction, so it must not claim a reaction trigger.
    CHECK_EQ(last.triggerSequence, std::uint64_t{0});
    // The command is replay substrate: it has to occupy a real revision slot.
    CHECK(last.resultingRevision > last.expectedRevision);

    // The event stream is the other observable face of the same fact.
    eve::GameplaySession session{"ability-test", eve::GameplayAccess::TestDriver, {fixture.unitSubject}};
    auto events = fixture.tactics.gameplayEvents(session, fixture.battleSubject, 0);
    REQUIRE(events.ok());
    const auto declaredEvents = std::move(events).takeValue();
    const auto found = std::find_if(declaredEvents.begin(), declaredEvents.end(),
                                    [](const eve::GameplayEvent& event) { return event.type == "action.declared"; });
    REQUIRE(found != declaredEvents.end());
    CHECK_EQ(found->subject, fixture.unitSubject);
}

TEST_CASE("tactics.abilityRefusesWhenActionPointsAreExhaustedWithoutMutating") {
    Fixture fixture;
    fixture.build(1);
    fixture.enterActing();

    REQUIRE(fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"), {1, 0, 0}).ok());
    CHECK_EQ(fixture.actionPoints(), 0);

    const auto commandsBefore = fixture.commands();

    auto refused = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                              {1, 0, 0});
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // A refusal must cost nothing and log nothing.
    CHECK_EQ(fixture.actionPoints(), 0);
    CHECK_EQ(fixture.commands().size(), commandsBefore.size());
}

TEST_CASE("tactics.abilityRefusesWrongActorLayerAndActionShape") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    // A different subject does not own the activation even when it exists.
    auto other = subject("00000000-0000-0000-0000-000000000503");
    auto wrongActor = fixture.tactics.useAbility(fixture.battle, other, action("test:strike"), {1, 0, 0});
    CHECK(!wrongActor.ok());
    REQUIRE(wrongActor.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(wrongActor.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // An invalid action id is a validation failure, not a precondition failure.
    auto emptyAction = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, eve::LogicalId{}, {1, 0, 0});
    CHECK(!emptyAction.ok());
    REQUIRE(emptyAction.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(emptyAction.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // An off-board layer cannot be a target: the board owns cell existence.
    auto wrongLayer = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                 {1, 0, 4});
    CHECK(!wrongLayer.ok());
    REQUIRE(wrongLayer.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(wrongLayer.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // Every refusal above must leave the activation untouched and unrecorded.
    CHECK_EQ(fixture.actionPoints(), 2);
    const auto commands = fixture.commands();
    CHECK(std::none_of(commands.begin(), commands.end(), [](const eve::tactics::BattleCommand& command) {
        return command.kind == eve::tactics::BattleCommandKind::UseAbility;
    }));
}
