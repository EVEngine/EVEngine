#include "climbing/ClimbingInput.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.input"));
}

bool commandLess(const BufferedClimbingCommand& lhs, const BufferedClimbingCommand& rhs) {
    if (lhs.pressedTick != rhs.pressedTick) return lhs.pressedTick < rhs.pressedTick;
    return static_cast<std::uint8_t>(lhs.command) < static_cast<std::uint8_t>(rhs.command);
}

}  // namespace

eve::Result<void> ClimbingInputSystem::submit(ClimbingIntent& intent, ClimbingCommand command,
                                              eve::SimulationTick pressedTick, std::uint64_t bufferTicks) {
    if (bufferTicks == 0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "input buffer must contain at least one tick",
                             "bufferTicks");
    if (pressedTick.value() > std::numeric_limits<std::uint64_t>::max() - bufferTicks)
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "input expiry tick would overflow", "pressedTick");
    (void)prune(intent, pressedTick);
    if (std::any_of(intent.commands.begin(), intent.commands.end(),
                    [&](const auto& entry) { return entry.command == command && entry.pressedTick == pressedTick; }))
        return failure<void>(eve::DiagnosticCode::Conflict, "duplicate edge command for the same tick", "command");
    if (intent.commands.size() >= MaxBufferedCommands)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation, "climbing input buffer is full", "commands");
    intent.commands.push_back(
        {command, pressedTick, eve::SimulationTick(pressedTick.value() + bufferTicks), ClimbingExecutionId::zero()});
    std::stable_sort(intent.commands.begin(), intent.commands.end(), commandLess);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingInputSystem::submit(ClimbingIntent& intent, ClimbingCommand command,
                                              eve::SimulationTick              pressedTick,
                                              const ClimbingProfileDefinition& profile) {
    return submit(intent, command, pressedTick, profile.inputBufferTicks);
}

std::size_t ClimbingInputSystem::prune(ClimbingIntent& intent, eve::SimulationTick currentTick) noexcept {
    const std::size_t before = intent.commands.size();
    std::erase_if(intent.commands, [&](const BufferedClimbingCommand& entry) {
        return !entry.consumedExecutionId.isZero() || entry.expiryTick < currentTick;
    });
    return before - intent.commands.size();
}

eve::Result<std::optional<BufferedClimbingCommand>> ClimbingInputSystem::consume(ClimbingIntent&     intent,
                                                                                 ClimbingCommand     command,
                                                                                 eve::SimulationTick currentTick,
                                                                                 ClimbingExecutionId executionId) {
    if (executionId.isZero())
        return failure<std::optional<BufferedClimbingCommand>>(
            eve::DiagnosticCode::InvalidArgument, "consuming execution id must be non-zero", "executionId");
    (void)prune(intent, currentTick);
    const auto found = std::find_if(intent.commands.begin(), intent.commands.end(), [&](const auto& entry) {
        return entry.command == command && entry.pressedTick <= currentTick && currentTick <= entry.expiryTick &&
               entry.consumedExecutionId.isZero();
    });
    if (found == intent.commands.end())
        return eve::Result<std::optional<BufferedClimbingCommand>>::success(
            std::nullopt, eve::Status::success(eve::StatusCode::NoOp));
    found->consumedExecutionId = executionId;
    return eve::Result<std::optional<BufferedClimbingCommand>>::success(*found,
                                                                        eve::Status::success(eve::StatusCode::Applied));
}

std::optional<BufferedClimbingCommand> ClimbingInputSystem::peek(const ClimbingIntent& intent, ClimbingCommand command,
                                                                 eve::SimulationTick currentTick) noexcept {
    const auto found = std::find_if(intent.commands.begin(), intent.commands.end(), [&](const auto& entry) {
        return entry.command == command && entry.pressedTick <= currentTick && currentTick <= entry.expiryTick &&
               entry.consumedExecutionId.isZero();
    });
    return found == intent.commands.end() ? std::nullopt : std::optional<BufferedClimbingCommand>(*found);
}

ClimbingCoyoteState ClimbingInputSystem::coyoteWindowState(eve::SimulationTick currentTick,
                                                            eve::SimulationTick lastGroundedTick,
                                                            std::uint64_t coyoteTicks) noexcept {
    if (currentTick < lastGroundedTick) return ClimbingCoyoteState::Outside;
    return currentTick.value() - lastGroundedTick.value() <= coyoteTicks ? ClimbingCoyoteState::Eligible
                                                                         : ClimbingCoyoteState::Outside;
}

}  // namespace eve::climbing
