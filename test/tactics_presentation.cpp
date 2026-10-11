#include "common/ECS.h"
#include "tactics/Presentation.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <utility>

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

const eve::SubjectRef kActor = subject("00000000-0000-0000-0000-000000000800");
const eve::SubjectRef kEnemy = subject("00000000-0000-0000-0000-000000000801");

eve::tactics::BattleEvent event(std::uint64_t sequence, const char* type, eve::SubjectRef subject,
                               std::uint64_t tick) {
    eve::tactics::BattleEvent value;
    value.sequence = sequence;
    value.type     = type;
    value.subject  = subject;
    value.tick     = eve::SimulationTick(tick);
    value.from     = eve::tactics::BattlePhase::Acting;
    value.to       = eve::tactics::BattlePhase::Acting;
    return value;
}

eve::tactics::PresentationFrame frame() {
    eve::tactics::PresentationFrame value;
    value.revision      = eve::Revision(9);
    value.transientTicks = 5;
    value.restingState  = {{kActor.format(), eve::tactics::UnitVisualState::Friendly},
                           {kEnemy.format(), eve::tactics::UnitVisualState::Idle}};
    return value;
}

}  // namespace

TEST_CASE("tactics.presentationProjectsTurnAndRoundBoundaries") {
    eve::tactics::PresentationProjector projector;
    const auto anchor = frame();

    auto pending = projector.project(event(3, "turn.pending", kActor, 10), nullptr, anchor);
    REQUIRE(pending.ok());
    REQUIRE_EQ(pending.value().size(), std::size_t{1});
    CHECK(pending.value()[0].intent.state == eve::tactics::UnitVisualState::Selected);
    CHECK_EQ(pending.value()[0].intent.subject, kActor);
    // Selection is transient and must say what clears it: the next turn, not a guess.
    CHECK(pending.value()[0].revert.isRevertable());
    CHECK(pending.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnNextTurn);
    CHECK_EQ(pending.value()[0].revert.restingState, eve::tactics::UnitVisualState::Friendly);

    auto completed = projector.project(event(4, "turn.completed", kActor, 11), nullptr, anchor);
    REQUIRE(completed.ok());
    REQUIRE_EQ(completed.value().size(), std::size_t{1});
    CHECK(completed.value()[0].intent.state == eve::tactics::UnitVisualState::Finished);
    // "Finished" lasts exactly one round: the boundary that clears it is stated, which is the
    // fact a view-object API cannot express.
    CHECK(completed.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnRoundStart);

    // The round boundary itself emits nothing new; it is the trigger consumers watch.
    auto roundPending = projector.project(event(5, "round.pending", {}, 12), nullptr, anchor);
    REQUIRE(roundPending.ok());
    CHECK(roundPending.value().empty());
}

TEST_CASE("tactics.presentationTakesGeometryFromTheCommandNotTheEvent") {
    eve::tactics::PresentationProjector projector;
    const auto anchor = frame();

    eve::tactics::BattleCommand move;
    move.kind = eve::tactics::BattleCommandKind::Move;
    move.actor = kActor;
    move.cell  = {1, 0, 0};

    auto moved = projector.project(event(6, "unit.moved", kActor, 20), &move, anchor);
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value().size(), std::size_t{1});
    CHECK(moved.value()[0].intent.state == eve::tactics::UnitVisualState::Moving);
    const eve::tactics::Cell destination{1, 0, 0};
    CHECK(moved.value()[0].intent.to == destination);
    CHECK_EQ(moved.value()[0].intent.path.size(), std::size_t{1});
    // Movement is a short animation, so it expires instead of lingering.
    CHECK(moved.value()[0].revert.trigger == eve::tactics::PresentationRevertTrigger::OnExpiry);
    CHECK_EQ(moved.value()[0].intent.expiresAtTick.value(), std::uint64_t{25});

    eve::tactics::BattleCommand ability;
    ability.kind       = eve::tactics::BattleCommandKind::UseAbility;
    ability.actor      = kActor;
    ability.cell       = {2, 0, 0};
    ability.targetUnit = kEnemy;
    ability.payload    = "{\"power\":4}";

    auto declared = projector.project(event(7, "action.declared", kActor, 30), &ability, anchor);
    REQUIRE(declared.ok());
    REQUIRE_EQ(declared.value().size(), std::size_t{1});
    CHECK(declared.value()[0].intent.state == eve::tactics::UnitVisualState::Attacking);
    CHECK_EQ(declared.value()[0].intent.other, kEnemy);
    const eve::tactics::Cell targetCell{2, 0, 0};
    CHECK(declared.value()[0].intent.to == targetCell);

    // Without the command the intent is still emitted, just without geometry: guessing the
    // destination from the event would be inventing a second authority for it.
    auto geometryless = projector.project(event(8, "unit.moved", kActor, 31), nullptr, anchor);
    REQUIRE(geometryless.ok());
    REQUIRE_EQ(geometryless.value().size(), std::size_t{1});
    CHECK(geometryless.value()[0].intent.path.empty());
}

TEST_CASE("tactics.presentationRevertContractIsExplicitAndRefusesTheImpossible") {
    eve::tactics::PresentationProjector projector;
    const auto anchor = frame();

    auto defeated = projector.project(event(9, "unit.defeated", kEnemy, 40), nullptr, anchor);
    REQUIRE(defeated.ok());
    REQUIRE_EQ(defeated.value().size(), std::size_t{1});
    const auto& command = defeated.value()[0];
    CHECK(command.intent.state == eve::tactics::UnitVisualState::Destroyed);
    // A defeat is durable: no trigger, and no deadline pretending there is one.
    CHECK(!command.revert.isRevertable());
    CHECK(command.revert.trigger == eve::tactics::PresentationRevertTrigger::Never);
    CHECK_EQ(command.intent.expiresAtTick.value(), std::uint64_t{0});

    // Asking to revert it is a refusal, not a silent no-op that would leave the view lying.
    auto impossible = projector.revert(command.intent, command.revert);
    CHECK(!impossible.ok());
    REQUIRE(impossible.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(impossible.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);

    // A transient intent does revert, back to the subject's resting state.
    auto selected = projector.project(event(10, "turn.pending", kActor, 41), nullptr, anchor);
    REQUIRE(selected.ok());
    REQUIRE_EQ(selected.value().size(), std::size_t{1});
    auto inverse = projector.revert(selected.value()[0].intent, selected.value()[0].revert);
    REQUIRE(inverse.ok());
    CHECK(inverse.value().state == eve::tactics::UnitVisualState::Friendly);
    CHECK_EQ(inverse.value().subject, kActor);
    CHECK_EQ(inverse.value().sequence, std::uint64_t{10});
    // The undo is itself transient, or it would become the next stale state.
    CHECK_EQ(inverse.value().expiresAtTick.value(), selected.value()[0].intent.expiresAtTick.value());
}

TEST_CASE("tactics.presentationIgnoresEventsWithNoVisualMeaning") {
    eve::tactics::PresentationProjector projector;
    const auto anchor = frame();
    // A random draw or an objective evaluation changes nothing a player sees; an empty
    // projection is the correct answer, not an error.
    for (const char* type : {"random.rolled", "objective.completed", "battle.started"}) {
        auto projected = projector.project(event(11, type, kActor, 50), nullptr, anchor);
        REQUIRE(projected.ok());
        CHECK(projected.value().empty());
    }
    // Stable protocol spellings, so a consumer may switch on them.
    CHECK_EQ(eve::tactics::unitVisualStateName(eve::tactics::UnitVisualState::Destroyed),
             std::string_view("destroyed"));
    CHECK_EQ(eve::tactics::presentationRevertTriggerName(eve::tactics::PresentationRevertTrigger::OnRoundStart),
             std::string_view("on_round_start"));
}
