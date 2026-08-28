#include "tactics/TacticsReplay.h"

#include <utility>

namespace eve::tactics {
namespace {

Result<void> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
Result<void> consume(Result<T>&& result) {
    if (!result) return Result<void>::failure(result.status());
    std::move(result).takeValue();
    return Result<void>::success();
}

Result<void> apply(Battle& battle, const BattleCommand& command) {
    switch (command.kind) {
        case BattleCommandKind::Start: return BattleSystem::start(battle, command.policy);
        case BattleCommandKind::Advance: return consume(BattleSystem::advance(battle, command.step));
        case BattleCommandKind::Move:
            return consume(BattleSystem::moveUnit(battle, command.actor, command.cell, command.step.tick));
        case BattleCommandKind::Face:
            return BattleSystem::faceUnit(battle, command.actor, command.facing, command.step.tick);
        case BattleCommandKind::Wait: return BattleSystem::waitUnit(battle, command.actor, command.step.tick);
        case BattleCommandKind::EndTurn: return BattleSystem::endTurn(battle, command.actor);
        case BattleCommandKind::Finish: return BattleSystem::finish(battle);
        case BattleCommandKind::OpenReaction:
            return consume(BattleSystem::openReaction(battle, command.triggerSequence, command.candidates));
        case BattleCommandKind::AcceptReaction:
            return consume(BattleSystem::acceptReaction(battle, command.actor, command.action));
        case BattleCommandKind::DeclineReaction: return BattleSystem::declineReaction(battle);
        case BattleCommandKind::DefeatUnit: return BattleSystem::defeatUnit(battle, command.actor);
        case BattleCommandKind::RollRandom: return consume(BattleSystem::roll(battle, command.action));
    }
    return fail(DiagnosticCode::UnknownVersion, "unknown tactics replay command kind", "command.kind");
}

}  // namespace

std::vector<BattleCommand> BattleReplay::commandsFrom(Battle& battle, Revision fromRevision) {
    std::vector<BattleCommand> result;
    for (const auto& command : battle.commands()->values)
        if (command.resultingRevision > fromRevision) result.push_back(command);
    return result;
}

Result<void> BattleReplay::replay(Battle& battle, std::span<const BattleCommand> commands) {
    if (commands.empty()) return Result<void>::success(Status::success(StatusCode::NoOp));
    std::uint64_t previousSequence = 0;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const BattleCommand& command = commands[index];
        const std::string path = "commands[" + std::to_string(index) + "]";
        if (command.sequence == 0 || (index > 0 && command.sequence != previousSequence + 1))
            return fail(DiagnosticCode::Conflict, "tactics replay command sequence is not contiguous", path);
        if (battle.commands()->nextSequence != command.sequence)
            return fail(DiagnosticCode::Conflict, "tactics replay command sequence differs from target", path);
        if (battle.turn()->revision != command.expectedRevision)
            return fail(DiagnosticCode::Conflict, "tactics replay expected revision differs from target", path);
        auto applied = apply(battle, command);
        if (!applied) return Result<void>::failure(applied.status());
        if (battle.turn()->revision != command.resultingRevision)
            return fail(DiagnosticCode::InvariantViolation, "tactics replay produced a different revision", path);
        const auto& recorded = battle.commands()->values.back();
        if (recorded.kind != command.kind || recorded.sequence != command.sequence ||
            recorded.expectedRevision != command.expectedRevision ||
            recorded.resultingRevision != command.resultingRevision)
            return fail(DiagnosticCode::InvariantViolation, "tactics replay recorded a different command", path);
        previousSequence = command.sequence;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::tactics
