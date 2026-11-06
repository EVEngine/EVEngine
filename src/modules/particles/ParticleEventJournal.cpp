#include "particles/ParticleEventJournal.h"
#include <cmath>
#include <new>
namespace eve::particles {
Result<void> ParticleEventJournal::prepare(std::size_t maximumEvents) {
    if (!maximumEvents || maximumEvents > 2000000)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "invalid particle recording event budget", "particleJournal"));
    try {
        std::vector<ParticleRecordEvent> candidate;
        candidate.reserve(maximumEvents);
        events_.swap(candidate);
        limit_ = maximumEvents;
        state_ = ParticleRecordState::Recording;
        return Result<void>::success();
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "particle recording allocation failed", "particleJournal"));
    }
}
Result<void> ParticleEventJournal::append(ParticleRecordEvent event) {
    if (state_ == ParticleRecordState::Disabled) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (state_ != ParticleRecordState::Recording)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "particle recording already failed", "particleJournal"));
    bool valid = std::isfinite(event.seconds) && event.seconds >= 0 && event.birthSerial != 0 &&
                 (events_.empty() || event.seconds >= events_.back().seconds) &&
                 (event.kind == ParticleRecordEventKind::Snapshot || event.kind == ParticleRecordEventKind::Death);
    const auto& p = event.appearance;
    for (float value :
         {p.x, p.y, p.w, p.h, p.rotation, p.color.r, p.color.g, p.color.b, p.color.a, p.u0, p.v0, p.u1, p.v1})
        valid = valid && std::isfinite(value);
    if (!valid) {
        state_ = ParticleRecordState::InvalidInput;
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid particle recording event", "particleJournal"));
    }
    if (events_.size() == limit_) {
        state_ = ParticleRecordState::Exhausted;
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "particle recording event budget exhausted", "particleJournal"));
    }
    events_.push_back(event);
    return Result<void>::success();
}
Result<std::vector<ParticleRecordEvent>> ParticleEventJournal::finish() {
    if (state_ != ParticleRecordState::Recording)
        return Result<std::vector<ParticleRecordEvent>>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "particle recording is not completeable", "particleJournal"));
    auto result = std::move(events_);
    events_.clear();
    limit_ = 0;
    state_ = ParticleRecordState::Disabled;
    return Result<std::vector<ParticleRecordEvent>>::success(std::move(result));
}
void ParticleEventJournal::invalidate() noexcept {
    if (state_ == ParticleRecordState::Recording) state_ = ParticleRecordState::InvalidInput;
}
void ParticleEventJournal::disable() noexcept {
    if (state_ == ParticleRecordState::Recording) state_ = ParticleRecordState::Disabled;
}
ParticleRecordState                  ParticleEventJournal::state() const noexcept { return state_; }
std::span<const ParticleRecordEvent> ParticleEventJournal::events() const noexcept { return events_; }
}  // namespace eve::particles
