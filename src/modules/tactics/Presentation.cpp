/**
 * @file Presentation.cpp
 * @brief Battle events projected into bounded, revertible presentation intents.
 */

#include "tactics/Presentation.h"

#include "common/Diagnostic.h"

#include <utility>

namespace eve::tactics {
namespace {

/** @brief Resting state of a subject, defaulting to the neutral resting state. */
UnitVisualState restingFor(const PresentationFrame& frame, SubjectRef subject) {
    const auto found = frame.restingState.find(subject.format());
    return found == frame.restingState.end() ? UnitVisualState::Idle : found->second;
}

PresentationCommand revertible(const BattleEvent& event, const PresentationFrame& frame, SubjectRef subject,
                              UnitVisualState state, PresentationRevertTrigger trigger) {
    PresentationCommand command;
    command.intent.sequence = event.sequence;
    command.intent.subject  = subject;
    command.intent.state    = state;
    command.intent.revision = frame.revision;
    command.intent.tick     = event.tick;
    command.revert.restingState = restingFor(frame, subject);
    command.revert.trigger      = trigger;
    command.revert.sequence     = event.sequence;
    if (trigger == PresentationRevertTrigger::OnExpiry)
        command.intent.expiresAtTick = SimulationTick(event.tick.value() + frame.transientTicks);
    return command;
}

PresentationCommand durable(const BattleEvent& event, const PresentationFrame& frame, SubjectRef subject,
                           UnitVisualState state) {
    PresentationCommand command = revertible(event, frame, subject, state, PresentationRevertTrigger::Never);
    // A durable intent never expires, so it must not advertise a deadline.
    command.intent.expiresAtTick = SimulationTick::zero();
    return command;
}

}  // namespace

std::string_view unitVisualStateName(UnitVisualState state) noexcept {
    switch (state) {
        case UnitVisualState::Idle: return "idle";
        case UnitVisualState::Friendly: return "friendly";
        case UnitVisualState::Selected: return "selected";
        case UnitVisualState::Finished: return "finished";
        case UnitVisualState::Targetable: return "targetable";
        case UnitVisualState::Attacking: return "attacking";
        case UnitVisualState::Defending: return "defending";
        case UnitVisualState::Moving: return "moving";
        case UnitVisualState::Destroyed: return "destroyed";
    }
    return "idle";
}

std::string_view presentationRevertTriggerName(PresentationRevertTrigger trigger) noexcept {
    switch (trigger) {
        case PresentationRevertTrigger::Never: return "never";
        case PresentationRevertTrigger::OnExpiry: return "on_expiry";
        case PresentationRevertTrigger::OnNextTurn: return "on_next_turn";
        case PresentationRevertTrigger::OnRoundStart: return "on_round_start";
    }
    return "never";
}

Result<std::vector<PresentationCommand>> PresentationProjector::project(const BattleEvent& event,
                                                                       const BattleCommand* command,
                                                                       const PresentationFrame& frame) const {
    (void)transientTicks_;
    std::vector<PresentationCommand> commands;
    // The event type is the stable protocol spelling owned by the simulation; matching on it
    // keeps presentation a pure function of the event stream instead of a second opinion.
    if (event.type == "turn.pending") {
        commands.push_back(revertible(event, frame, event.subject, UnitVisualState::Selected,
                                      PresentationRevertTrigger::OnNextTurn));
    } else if (event.type == "turn.completed") {
        // Finishing an activation lasts until the round boundary, which is exactly the kind
        // of lifetime a view-object API cannot state.
        commands.push_back(revertible(event, frame, event.subject, UnitVisualState::Finished,
                                      PresentationRevertTrigger::OnRoundStart));
    } else if (event.type == "round.pending") {
        // The round boundary clears every per-round marker, so it emits no new intent; the
        // consumer reverts everything this trigger names.
        return Result<std::vector<PresentationCommand>>::success(std::move(commands));
    } else if (event.type == "unit.moved") {
        PresentationCommand moved = revertible(event, frame, event.subject, UnitVisualState::Moving,
                                               PresentationRevertTrigger::OnExpiry);
        if (command != nullptr && command->kind == BattleCommandKind::Move) {
            moved.intent.from = command->cell;
            moved.intent.to   = command->cell;
            moved.intent.path = {command->cell};
        }
        commands.push_back(std::move(moved));
    } else if (event.type == "action.declared") {
        PresentationCommand declared = revertible(event, frame, event.subject, UnitVisualState::Attacking,
                                                 PresentationRevertTrigger::OnExpiry);
        if (command != nullptr && command->kind == BattleCommandKind::UseAbility) {
            declared.intent.to    = command->cell;
            declared.intent.other = command->targetUnit;
        }
        commands.push_back(std::move(declared));
    } else if (event.type == "unit.defeated") {
        // Terminal: a destroyed unit is not coming back, so the revert is refused by contract.
        commands.push_back(durable(event, frame, event.subject, UnitVisualState::Destroyed));
    }
    return Result<std::vector<PresentationCommand>>::success(std::move(commands));
}

Result<PresentationIntent> PresentationProjector::revert(const PresentationIntent& intent,
                                                        const PresentationRevert& spec) const {
    if (!spec.isRevertable())
        return Result<PresentationIntent>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "durable presentation intent cannot be reverted", "presentation.revert"));
    PresentationIntent inverse;
    inverse.sequence = spec.sequence;
    inverse.subject  = intent.subject;
    inverse.other    = intent.other;
    inverse.state    = spec.restingState;
    inverse.from     = intent.to;
    inverse.to       = intent.from;
    inverse.revision = intent.revision;
    inverse.tick     = intent.expiresAtTick;
    // The inverse is itself a transient instruction, so it must expire too; otherwise the
    // "undo" would become the next stale state.
    inverse.expiresAtTick = intent.expiresAtTick;
    return Result<PresentationIntent>::success(std::move(inverse));
}

}  // namespace eve::tactics
