/**
 * @file Interaction.cpp
 * @brief Implementation of the board interaction state machine.
 */

#include "tactics/Interaction.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool containsCell(const std::vector<Cell>& cells, Cell cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

}  // namespace

std::string_view interactionStateName(InteractionState state) noexcept {
    switch (state) {
        case InteractionState::Blocked: return "blocked";
        case InteractionState::AwaitSelection: return "await_selection";
        case InteractionState::UnitSelected: return "unit_selected";
        case InteractionState::Targeting: return "targeting";
        case InteractionState::Resolving: return "resolving";
        case InteractionState::Ended: return "ended";
    }
    return "blocked";
}

std::string_view interactionIntentKindName(InteractionIntentKind kind) noexcept {
    switch (kind) {
        case InteractionIntentKind::None: return "none";
        case InteractionIntentKind::SelectUnit: return "select_unit";
        case InteractionIntentKind::MoveTo: return "move_to";
        case InteractionIntentKind::UseAbilityOn: return "use_ability_on";
        case InteractionIntentKind::Cancel: return "cancel";
        case InteractionIntentKind::Confirm: return "confirm";
        case InteractionIntentKind::EndTurn: return "end_turn";
    }
    return "none";
}

InteractionSession::InteractionSession(InteractionContext context) : context_(std::move(context)) {
    // The initial state is computed once from the context; every later transition is an
    // explicit handler, so there is exactly one place that decides "can the player act".
    if (context_.status != BattleStatus::Running) {
        state_ = context_.status == BattleStatus::Ended ? InteractionState::Ended : InteractionState::Blocked;
    } else if (context_.phase != BattlePhase::Acting) {
        state_ = InteractionState::Blocked;
    } else if (!context_.controlledUnit.isValid() || context_.activeUnit != context_.controlledUnit) {
        state_ = InteractionState::Blocked;
    } else {
        state_ = InteractionState::AwaitSelection;
    }
}

InteractionSession InteractionSession::create(InteractionContext context) {
    return InteractionSession(std::move(context));
}

InteractionState InteractionSession::state() const noexcept { return state_; }

bool InteractionSession::acceptsInput() const noexcept {
    return state_ != InteractionState::Blocked && state_ != InteractionState::Ended &&
           state_ != InteractionState::Resolving;
}

bool InteractionSession::cellInReachable(Cell cell) const { return containsCell(context_.reachableCells, cell); }

bool InteractionSession::cellInTargetable(Cell cell) const { return containsCell(armedTargets_, cell); }

InteractionIntent InteractionSession::makeIntent(InteractionIntentKind kind, Cell cell) const {
    InteractionIntent intent;
    intent.kind = kind;
    intent.actor = context_.controlledUnit;
    intent.cell = cell;
    intent.action = armedAction_;
    const auto occupant = context_.unitsByCell.find(cell);
    if (occupant != context_.unitsByCell.end()) intent.targetUnit = occupant->second;
    intent.expected = context_.revision;
    return intent;
}

Result<InteractionIntent> InteractionSession::onCellClicked(Cell cell) {
    if (state_ == InteractionState::Ended)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session belongs to a finished battle", "interaction.state");
    if (state_ == InteractionState::Blocked)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session is blocked", "interaction.state");
    if (state_ == InteractionState::Resolving)
        return failure<InteractionIntent>(DiagnosticCode::Conflict,
                                          "a pending interaction intent must be committed or dropped first",
                                          "interaction.state");

    if (state_ == InteractionState::Targeting) {
        if (!cellInTargetable(cell)) return Result<InteractionIntent>::success(makeIntent(InteractionIntentKind::None, cell));
        // The intent names both the cell and the occupying unit, so the caller can commit
        // it without re-deriving which unit the click landed on.
        pending_ = InteractionIntentKind::UseAbilityOn;
        state_ = InteractionState::Resolving;
        return Result<InteractionIntent>::success(makeIntent(pending_, cell));
    }

    // AwaitSelection / UnitSelected.
    if (cell == context_.controlledCell) {
        unitSelected_ = true;
        state_ = InteractionState::UnitSelected;
        pending_ = InteractionIntentKind::SelectUnit;
        return Result<InteractionIntent>::success(makeIntent(pending_, cell));
    }
    if (cellInReachable(cell)) {
        // A move is a complete intention on its own, so the machine reports it and lets the
        // caller commit; the session is expected to be resolved or rebuilt afterwards.
        pending_ = InteractionIntentKind::MoveTo;
        state_ = InteractionState::Resolving;
        return Result<InteractionIntent>::success(makeIntent(pending_, cell));
    }
    return Result<InteractionIntent>::success(makeIntent(InteractionIntentKind::None, cell));
}

Result<void> InteractionSession::armAbility(const LogicalId& action, std::vector<Cell> targetableCells) {
    if (state_ == InteractionState::Ended || state_ == InteractionState::Blocked || state_ == InteractionState::Resolving)
        return failure<void>(DiagnosticCode::PreconditionViolation, "interaction session cannot arm an ability",
                             "interaction.state");
    if (!action.isValid())
        return failure<void>(DiagnosticCode::InvalidArgument, "armed ability requires a logical id", "action");
    armedAction_ = action;
    armedTargets_ = std::move(targetableCells);
    unitSelected_ = true;
    state_ = InteractionState::Targeting;
    pending_ = InteractionIntentKind::None;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<InteractionIntent> InteractionSession::onCancel() {
    if (state_ == InteractionState::Ended)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session belongs to a finished battle", "interaction.state");
    if (state_ == InteractionState::Blocked)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session is blocked", "interaction.state");
    if (state_ == InteractionState::Resolving)
        return failure<InteractionIntent>(DiagnosticCode::Conflict,
                                          "a pending interaction intent must be committed or dropped first",
                                          "interaction.state");
    if (state_ == InteractionState::AwaitSelection && !unitSelected_)
        // Nothing to cancel is not a failure, but it must not be reported as if the player
        // had cancelled something either.
        return Result<InteractionIntent>::success(makeIntent(InteractionIntentKind::None, context_.controlledCell));

    armedAction_ = LogicalId{};
    armedTargets_.clear();
    unitSelected_ = false;
    pending_ = InteractionIntentKind::None;
    state_ = InteractionState::AwaitSelection;
    return Result<InteractionIntent>::success(makeIntent(InteractionIntentKind::Cancel, context_.controlledCell));
}

Result<InteractionIntent> InteractionSession::onEndTurn() {
    if (state_ == InteractionState::Ended)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session belongs to a finished battle", "interaction.state");
    if (state_ == InteractionState::Blocked || state_ == InteractionState::Resolving)
        return failure<InteractionIntent>(DiagnosticCode::PreconditionViolation,
                                          "interaction session cannot end the turn", "interaction.state");
    // Ending the turn is terminal for this activation, so the session moves to Resolving and
    // the caller decides whether to commit it or rebuild from a fresh context.
    pending_ = InteractionIntentKind::EndTurn;
    armedAction_ = LogicalId{};
    armedTargets_.clear();
    unitSelected_ = false;
    state_ = InteractionState::Resolving;
    InteractionIntent intent = makeIntent(pending_, context_.controlledCell);
    // `makeIntent` echoed the armed action, which this handler just cleared.
    intent.action = LogicalId{};
    return Result<InteractionIntent>::success(std::move(intent));
}

Result<void> InteractionSession::markResolving() {
    if (state_ == InteractionState::Ended || state_ == InteractionState::Blocked)
        return failure<void>(DiagnosticCode::PreconditionViolation, "interaction session cannot resolve",
                             "interaction.state");
    if (pending_ == InteractionIntentKind::None)
        return failure<void>(DiagnosticCode::PreconditionViolation,
                             "interaction session has no pending intent to confirm", "interaction.pending");
    state_ = InteractionState::Resolving;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> InteractionSession::resolve() {
    if (state_ == InteractionState::Ended || state_ == InteractionState::Blocked)
        return failure<void>(DiagnosticCode::PreconditionViolation, "interaction session cannot resolve",
                             "interaction.state");
    armedAction_ = LogicalId{};
    armedTargets_.clear();
    unitSelected_ = false;
    pending_ = InteractionIntentKind::None;
    state_ = InteractionState::AwaitSelection;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::tactics
