#include "common/ECS.h"
#include "tactics/Interaction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <utility>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

const eve::SubjectRef kActor  = subject("00000000-0000-0000-0000-000000000700");
const eve::SubjectRef kAlly   = subject("00000000-0000-0000-0000-000000000701");
const eve::SubjectRef kEnemy  = subject("00000000-0000-0000-0000-000000000702");

/** @brief A context where the controlled unit is the one allowed to act. */
eve::tactics::InteractionContext actingContext() {
    eve::tactics::InteractionContext context;
    context.status = eve::tactics::BattleStatus::Running;
    context.phase  = eve::tactics::BattlePhase::Acting;
    context.activeUnit     = kActor;
    context.controlledUnit = kActor;
    context.controlledCell = {0, 0, 0};
    context.reachableCells = {{1, 0, 0}, {0, 1, 0}};
    context.unitsByCell    = {{{0, 0, 0}, kActor}, {{2, 0, 0}, kEnemy}, {{0, 2, 0}, kAlly}};
    context.revision       = eve::Revision(7);
    return context;
}

}  // namespace

TEST_CASE("tactics.interactionSessionIsBlockedExceptOnTheActiveUnitsTurn") {
    // Not running yet.
    auto pending = actingContext();
    pending.status = eve::tactics::BattleStatus::Setup;
    auto setup = eve::tactics::InteractionSession::create(pending);
    CHECK(setup.state() == eve::tactics::InteractionState::Blocked);
    auto refused = setup.onCellClicked({1, 0, 0});
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // Running, but somebody else is acting: the session exists and says so instead of
    // letting a click through.
    auto otherTurn = actingContext();
    otherTurn.activeUnit = kEnemy;
    auto waiting = eve::tactics::InteractionSession::create(otherTurn);
    CHECK(waiting.state() == eve::tactics::InteractionState::Blocked);
    CHECK(!waiting.onCellClicked({1, 0, 0}).ok());

    // Finished battle.
    auto finished = actingContext();
    finished.status = eve::tactics::BattleStatus::Ended;
    finished.phase  = eve::tactics::BattlePhase::BattleEnd;
    auto over = eve::tactics::InteractionSession::create(finished);
    CHECK(over.state() == eve::tactics::InteractionState::Ended);
    CHECK(!over.onEndTurn().ok());

    // The acting unit's turn: the session accepts input.
    auto live = eve::tactics::InteractionSession::create(actingContext());
    CHECK(live.state() == eve::tactics::InteractionState::AwaitSelection);
}

TEST_CASE("tactics.interactionSessionReturnsIntentsWithoutMutatingTheBattle") {
    auto session = eve::tactics::InteractionSession::create(actingContext());

    // Clicking the controlled unit selects it and reports the revision it decided against.
    auto selected = session.onCellClicked({0, 0, 0});
    REQUIRE(selected.ok());
    CHECK(selected.value().kind == eve::tactics::InteractionIntentKind::SelectUnit);
    CHECK_EQ(selected.value().actor, kActor);
    CHECK_EQ(selected.value().expected.value(), std::uint64_t{7});
    CHECK(session.state() == eve::tactics::InteractionState::UnitSelected);

    // A second click on a reachable cell turns into a move intention.
    auto move = session.onCellClicked({1, 0, 0});
    REQUIRE(move.ok());
    CHECK(move.value().kind == eve::tactics::InteractionIntentKind::MoveTo);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(move.value().cell == destination);
    CHECK(session.state() == eve::tactics::InteractionState::Resolving);

    // A pending intention must be committed or dropped before a new one is accepted.
    auto conflicting = session.onCellClicked({0, 1, 0});
    CHECK(!conflicting.ok());
    REQUIRE(conflicting.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(conflicting.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    CHECK(session.resolve().ok());
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);

    // A click on an unreachable, unoccupied cell is normal, not an error.
    auto nothing = session.onCellClicked({5, 5, 0});
    REQUIRE(nothing.ok());
    CHECK(nothing.value().kind == eve::tactics::InteractionIntentKind::None);
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);
}

TEST_CASE("tactics.interactionTargetingResolvesTheUnitUnderTheCell") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    // Arming with no legal target is allowed: a UI can show the ability and get `None` for
    // every click instead of a failure that reads like a bug.
    REQUIRE(session.armAbility(action("test:strike"), {}).ok());
    CHECK(session.state() == eve::tactics::InteractionState::Targeting);
    auto noTarget = session.onCellClicked({2, 0, 0});
    REQUIRE(noTarget.ok());
    CHECK(noTarget.value().kind == eve::tactics::InteractionIntentKind::None);

    // Re-arm with a legal target set.
    REQUIRE(session.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    auto declared = session.onCellClicked({2, 0, 0});
    REQUIRE(declared.ok());
    CHECK(declared.value().kind == eve::tactics::InteractionIntentKind::UseAbilityOn);
    CHECK_EQ(declared.value().action.format(), std::string("test:strike"));
    // The intent names the unit under the cell, so the caller does not re-derive it — and
    // getting that lookup wrong is exactly the class of bug this machine exists to prevent.
    CHECK_EQ(declared.value().targetUnit, kEnemy);

    // An empty target cell still yields the intent, with no unit attached.
    REQUIRE(session.resolve().ok());
    REQUIRE(session.armAbility(action("test:strike"), {{3, 0, 0}}).ok());
    auto emptyCell = session.onCellClicked({3, 0, 0});
    REQUIRE(emptyCell.ok());
    CHECK(emptyCell.value().kind == eve::tactics::InteractionIntentKind::UseAbilityOn);
    CHECK(!emptyCell.value().targetUnit.isValid());
}

TEST_CASE("tactics.interactionCancelAndEndTurnAreExplicit") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    // Nothing armed or selected: cancelling reports "nothing to do" rather than pretending
    // the player cancelled something.
    auto idleCancel = session.onCancel();
    REQUIRE(idleCancel.ok());
    CHECK(idleCancel.value().kind == eve::tactics::InteractionIntentKind::None);

    REQUIRE(session.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    auto cancelled = session.onCancel();
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value().kind == eve::tactics::InteractionIntentKind::Cancel);
    CHECK(session.state() == eve::tactics::InteractionState::AwaitSelection);
    CHECK(!session.armedAction().isValid());

    auto ended = session.onEndTurn();
    REQUIRE(ended.ok());
    CHECK(ended.value().kind == eve::tactics::InteractionIntentKind::EndTurn);
    CHECK_EQ(ended.value().actor, kActor);
    CHECK(session.state() == eve::tactics::InteractionState::Resolving);
    // Ending the turn is not confirmable: it is already a complete instruction.
    CHECK(session.resolve().ok());
}

TEST_CASE("tactics.interactionRejectsInvalidActions") {
    auto session = eve::tactics::InteractionSession::create(actingContext());
    auto invalid = session.armAbility(eve::LogicalId{}, {{2, 0, 0}});
    CHECK(!invalid.ok());
    REQUIRE(invalid.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(invalid.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // Confirming without a pending intent is a refusal, not a silent success.
    auto nothingPending = session.markResolving();
    CHECK(!nothingPending.ok());
    REQUIRE(nothingPending.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(nothingPending.status().primaryDiagnostic()->code(), eve::DiagnosticCode::PreconditionViolation);

    // Blocked sessions refuse everything except inspection.
    auto blockedContext = actingContext();
    blockedContext.phase = eve::tactics::BattlePhase::RoundStart;
    auto blocked = eve::tactics::InteractionSession::create(blockedContext);
    CHECK(!blocked.armAbility(action("test:strike"), {{2, 0, 0}}).ok());
    CHECK(!blocked.onCancel().ok());
    CHECK(!blocked.markResolving().ok());

    // The protocol spellings are stable text, so a caller may log or serialize them.
    CHECK_EQ(eve::tactics::interactionStateName(eve::tactics::InteractionState::Targeting),
             std::string_view("targeting"));
    CHECK_EQ(eve::tactics::interactionIntentKindName(eve::tactics::InteractionIntentKind::UseAbilityOn),
             std::string_view("use_ability_on"));
}
