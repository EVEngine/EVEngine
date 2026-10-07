#include "combat/HitFeel.h"

#include <algorithm>
#include <utility>

namespace eve::combat {
namespace {

Duration maxDuration(Duration a, Duration b) { return a < b ? b : a; }

}  // namespace

Result<void> HitFeelRequest::validate() const {
    if (!attacker.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attacker is nil"), std::move("attacker")));
    if (!victim.isValid()) return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("victim is nil"), std::move("victim")));
    if (attackerHitstop < Duration::zero() || victimHitstop < Duration::zero() ||
        victimHitstun < Duration::zero())
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("hit-feel durations must be non-negative"), std::move("duration")));
    return Result<void>::success();
}

Result<void> HitFeelRuntime::apply(HitFeelRequest request) {
    auto valid = request.validate();
    if (!valid) return valid;
    auto& attacker = states_[request.attacker.format()];
    attacker.subject = request.attacker;
    attacker.hitstopRemaining = maxDuration(attacker.hitstopRemaining, request.attackerHitstop);
    attacker.timeFrozen = attacker.hitstopRemaining > Duration::zero();

    auto& victim = states_[request.victim.format()];
    victim.subject = request.victim;
    victim.hitstopRemaining = maxDuration(victim.hitstopRemaining, request.victimHitstop);
    victim.hitstunRemaining = maxDuration(victim.hitstunRemaining, request.victimHitstun);
    victim.timeFrozen = victim.hitstopRemaining > Duration::zero();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> HitFeelRuntime::applyFromOutcome(const DamageOutcome& outcome) {
    HitFeelRequest request;
    request.attacker = outcome.source;
    request.victim = outcome.target;
    switch (outcome.reaction) {
        case HitReaction::None:
            request.attackerHitstop = Duration::fromNanoseconds(16000000);
            request.victimHitstop = Duration::fromNanoseconds(16000000);
            break;
        case HitReaction::Flinch:
            request.attackerHitstop = Duration::fromNanoseconds(33000000);
            request.victimHitstop = Duration::fromNanoseconds(33000000);
            request.victimHitstun = Duration::fromNanoseconds(120000000);
            break;
        case HitReaction::Stagger:
            request.attackerHitstop = Duration::fromNanoseconds(50000000);
            request.victimHitstop = Duration::fromNanoseconds(50000000);
            request.victimHitstun = Duration::fromNanoseconds(350000000);
            break;
        case HitReaction::Knockdown:
            request.attackerHitstop = Duration::fromNanoseconds(66000000);
            request.victimHitstop = Duration::fromNanoseconds(66000000);
            request.victimHitstun = Duration::fromNanoseconds(800000000);
            break;
        case HitReaction::Death:
            request.attackerHitstop = Duration::fromNanoseconds(80000000);
            request.victimHitstop = Duration::fromNanoseconds(100000000);
            request.victimHitstun = Duration::fromNanoseconds(1000000000);
            break;
    }
    request.reaction = outcome.reaction;
    return apply(std::move(request));
}

Result<HitFeelAdvance> HitFeelRuntime::advance(const SimulationStep& step) {
    if (step.tick.value() < lastTick_.value())
        return Result<HitFeelAdvance>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "hit-feel tick must be non-decreasing", "tick"));
    if (step.delta < Duration::zero())
        return Result<HitFeelAdvance>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "hit-feel delta is invalid", "delta"));

    HitFeelAdvance advance;
    advance.tick = step.tick;
    for (auto it = states_.begin(); it != states_.end();) {
        auto& state = it->second;
        const bool hadHitstop = state.hitstopRemaining > Duration::zero();
        const bool hadHitstun = state.hitstunRemaining > Duration::zero();
        if (state.hitstopRemaining > Duration::zero()) {
            state.hitstopRemaining = state.hitstopRemaining < step.delta
                                         ? Duration::zero()
                                         : Duration::fromNanoseconds(state.hitstopRemaining.nanoseconds() -
                                                                     step.delta.nanoseconds());
        }
        if (state.hitstunRemaining > Duration::zero()) {
            state.hitstunRemaining = state.hitstunRemaining < step.delta
                                         ? Duration::zero()
                                         : Duration::fromNanoseconds(state.hitstunRemaining.nanoseconds() -
                                                                     step.delta.nanoseconds());
        }
        state.timeFrozen = state.hitstopRemaining > Duration::zero();
        if (hadHitstop && state.hitstopRemaining.isZero())
            advance.events.push_back({state.subject, HitFeelEventKind::HitstopEnded, step.tick});
        if (hadHitstun && state.hitstunRemaining.isZero())
            advance.events.push_back({state.subject, HitFeelEventKind::HitstunEnded, step.tick});
        if (state.hitstopRemaining.isZero() && state.hitstunRemaining.isZero())
            it = states_.erase(it);
        else
            ++it;
    }
    lastTick_ = step.tick;
    return Result<HitFeelAdvance>::success(std::move(advance), Status::success(StatusCode::Applied));
}

HitFeelState HitFeelRuntime::state(SubjectRef subject) const {
    const auto found = states_.find(subject.format());
    if (found == states_.end()) return HitFeelState{subject, Duration::zero(), Duration::zero(), false};
    return found->second;
}

bool HitFeelRuntime::isFrozen(SubjectRef subject) const noexcept {
    const auto found = states_.find(subject.format());
    return found != states_.end() && found->second.hitstopRemaining > Duration::zero();
}

bool HitFeelRuntime::isStunned(SubjectRef subject) const noexcept {
    const auto found = states_.find(subject.format());
    return found != states_.end() && found->second.hitstunRemaining > Duration::zero();
}

}  // namespace eve::combat
