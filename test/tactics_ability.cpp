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

/** @brief One side with an acting unit and a slower ally, ready to declare abilities. */
struct Fixture {
    ecs::Table             world;
    ecs::ScopedTable       guard{world};
    eve::tactics::Tactics  tactics;
    ecs::EntityHandle      battle{};
    eve::SubjectRef        battleSubject = subject("00000000-0000-0000-0000-000000000500");
    eve::SubjectRef        sideSubject   = subject("00000000-0000-0000-0000-000000000501");
    eve::SubjectRef        unitSubject   = subject("00000000-0000-0000-0000-000000000502");
    // Lower initiative, so the acting unit is always `unitSubject` under every policy
    // this file uses, and the ally can be defeated or targeted without disturbing it.
    eve::SubjectRef        allySubject   = subject("00000000-0000-0000-0000-000000000503");

    /** @param actionPoints per-round action points, so exhaustion is reachable. */
    void build(int actionPoints = 2) {
        auto created = tactics.newBattle(battleSubject, 17);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {2, 0, 0}).ok());
        auto side = tactics.newSide(battle, sideSubject);
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0},
                                {actionPoints, 300, 0, 10})
                    .ok());
        REQUIRE(tactics.newUnit(battle, side.value(), allySubject, {}, {2, 0, 0}, {1, 300, 0, 5}).ok());
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

    // The declaration names both a target cell and a target unit, plus an opaque payload:
    // all three are tactical facts the effect owner reads back from the receipt or the
    // command log.
    auto declared = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                {1, 0, 0}, fixture.unitSubject, "{\"power\":3}");
    REQUIRE(declared.ok());
    // Applied, not NoOp: an ability that silently did nothing would still cost a point.
    CHECK_EQ(declared.code(), eve::StatusCode::Applied);
    CHECK_EQ(declared.value().remainingActionPoints, 1);
    CHECK_EQ(declared.value().action.format(), std::string("test:strike"));
    CHECK_EQ(declared.value().targetUnit, fixture.unitSubject);
    CHECK_EQ(fixture.actionPoints(), 1);

    const auto commands = fixture.commands();
    REQUIRE(!commands.empty());
    const auto& last = commands.back();
    CHECK(last.kind == eve::tactics::BattleCommandKind::UseAbility);
    CHECK_EQ(last.actor, fixture.unitSubject);
    CHECK_EQ(last.action.format(), std::string("test:strike"));
    const eve::tactics::Cell expectedTarget{1, 0, 0};
    CHECK(last.cell == expectedTarget);
    CHECK_EQ(last.targetUnit, fixture.unitSubject);
    CHECK_EQ(last.payload, std::string("{\"power\":3}"));
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
    // The event names the actor and points at the command that owns the rest of the
    // declaration, so a consumer joins the two instead of the framework copying the
    // action id, the targets and the payload into a second shape.
    CHECK_EQ(found->causationCommandId, "tactics:" + std::to_string(last.sequence));
}

TEST_CASE("tactics.abilityPreviewMatchesCommitWithoutMutating") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    auto preview = fixture.tactics.previewAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                  {1, 0, 0}, fixture.unitSubject, "{\"power\":3}");
    REQUIRE(preview.ok());
    CHECK_EQ(preview.value().remainingActionPoints, 1);
    // A preview is not a commitment: no point spent, no event, no command.
    CHECK_EQ(fixture.actionPoints(), 2);
    CHECK(fixture.commands().size() == std::size_t{4});   // start + three advances

    auto declared = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                {1, 0, 0}, fixture.unitSubject, "{\"power\":3}");
    REQUIRE(declared.ok());
    // The committed receipt must equal the previewed one, which is the whole point of
    // sharing a single validator.
    CHECK_EQ(declared.value().remainingActionPoints, preview.value().remainingActionPoints);
    CHECK_EQ(declared.value().targetUnit, preview.value().targetUnit);
    CHECK(declared.value().targetCell == preview.value().targetCell);
    CHECK_EQ(fixture.actionPoints(), 1);

    // A preview of something the commit would refuse must refuse it too, with the same
    // diagnostic: otherwise a UI could offer an activation the commit then rejects.
    auto refusedPreview = fixture.tactics.previewAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                         {1, 0, 4});
    auto refusedCommit = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                    {1, 0, 4});
    CHECK(!refusedPreview.ok());
    CHECK(!refusedCommit.ok());
    REQUIRE(refusedPreview.status().primaryDiagnostic() != nullptr);
    REQUIRE(refusedCommit.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refusedPreview.status().primaryDiagnostic()->code(),
             refusedCommit.status().primaryDiagnostic()->code());
    CHECK_EQ(fixture.actionPoints(), 1);
}

TEST_CASE("tactics.abilityRefusesTargetsAndPayloadsItCannotCarry") {
    Fixture fixture;
    fixture.build(2);
    fixture.enterActing();

    // A subject that is not a unit of this battle is a refusal, not a dropped target.
    const auto stranger = subject("00000000-0000-0000-0000-000000000599");
    auto foreignTarget = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                    {1, 0, 0}, stranger);
    CHECK(!foreignTarget.ok());
    REQUIRE(foreignTarget.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(foreignTarget.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    // A defeated unit cannot be targeted: a stale outcome must not read as "no target".
    REQUIRE(fixture.tactics.defeatUnit(fixture.battle, fixture.allySubject).ok());
    auto defeatedTarget = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                     {1, 0, 0}, fixture.allySubject);
    CHECK(!defeatedTarget.ok());
    REQUIRE(defeatedTarget.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(defeatedTarget.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // The payload is bounded because it is persisted: an oversized blob is refused
    // instead of silently truncating or unboundedly growing a snapshot.
    const std::string oversized(eve::tactics::kMaxAbilityPayloadBytes + 1, 'x');
    auto tooLarge = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                               {1, 0, 0}, {}, oversized);
    CHECK(!tooLarge.ok());
    REQUIRE(tooLarge.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(tooLarge.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // The exact bound is accepted, and so is an ability with no target unit at all.
    const std::string atBound(eve::tactics::kMaxAbilityPayloadBytes, 'x');
    auto atBoundResult = fixture.tactics.useAbility(fixture.battle, fixture.unitSubject, action("test:strike"),
                                                    {1, 0, 0}, {}, atBound);
    REQUIRE(atBoundResult.ok());
    CHECK(!atBoundResult.value().targetUnit.isValid());
    CHECK_EQ(fixture.commands().back().payload, atBound);
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
