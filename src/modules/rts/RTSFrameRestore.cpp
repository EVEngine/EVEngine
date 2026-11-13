#include "rts/RTSFrameRestore.h"
#include <array>
#include <optional>
#include "rts/RTS.h"
namespace eve::rts {
struct PreparedRTSFrameRestore::Impl {
    RTSFrameState state;
};
PreparedRTSFrameRestore::PreparedRTSFrameRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSFrameRestore::~PreparedRTSFrameRestore()                                                    = default;
PreparedRTSFrameRestore::PreparedRTSFrameRestore(PreparedRTSFrameRestore&&) noexcept                   = default;
PreparedRTSFrameRestore&        PreparedRTSFrameRestore::operator=(PreparedRTSFrameRestore&&) noexcept = default;
Result<PreparedRTSFrameRestore> PreparedRTSFrameRestore::prepare(const RTSFrameState& captured) {
    using R = Result<PreparedRTSFrameRestore>;
    // Merge by publication sequence without allocating a second event index.
    std::array<std::size_t, 3> rows{};
    const std::array           sizes{captured.damage.size(), captured.combat.size(), captured.lifecycle.size()};
    auto                       sequence = [&](std::size_t stream, std::size_t row) {
        if (stream == 0) return captured.damage[row].sequence;
        if (stream == 1) return captured.combat[row].sequence;
        return captured.lifecycle[row].sequence;
    };
    std::optional<std::uint64_t> previous;
    for (;;) {
        std::optional<std::size_t> selected;
        for (std::size_t stream = 0; stream < rows.size(); ++stream)
            if (rows[stream] < sizes[stream] &&
                (!selected || sequence(stream, rows[stream]) < sequence(*selected, rows[*selected])))
                selected = stream;
        if (!selected) break;
        const auto next = sequence(*selected, rows[*selected]++);
        if (next >= captured.sequence || (previous && next <= *previous))
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Invalid RTS frame event sequence"));
        previous = next;
    }
    if (captured.nextGameplaySequence == 0)
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Invalid RTS gameplay sequence watermark"));
    std::uint64_t priorGameplay = 0;
    for (const auto& event : captured.gameplay) {
        if (event.sequence <= priorGameplay || event.sequence >= captured.nextGameplaySequence)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Invalid RTS gameplay event sequence"));
        priorGameplay = event.sequence;
    }
    auto impl   = std::make_unique<Impl>();
    impl->state = captured;
    return R::success(PreparedRTSFrameRestore(std::move(impl)));
}
Result<void> RTS::restoreFrameState(PreparedRTSFrameRestore&& prepared) {
    if (!prepared.impl_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RTS frame token consumed"));
    if (!ownedWorld_)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "RTS frame restore requires owned Table"));
    if (ownedWorld_->is_deferred())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RTS frame restore needs idle Table"));
    auto& state = prepared.impl_->state;
    frameDamageEvents_.swap(state.damage);
    frameCombatEvents_.swap(state.combat);
    frameLifecycleEvents_.swap(state.lifecycle);
    gameplayEvents_.swap(state.gameplay);
    std::swap(frameEventSequence_, state.sequence);
    std::swap(preserveFrameEventsOnNextStep_, state.preserveOnNextStep);
    std::swap(nextGameplayEventSequence_, state.nextGameplaySequence);
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::rts
