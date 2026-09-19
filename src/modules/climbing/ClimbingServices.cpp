#include "climbing/ClimbingServices.h"

#include "common/Capability.h"
#include "physics/World3D.h"

namespace eve::climbing {
namespace {

bool commandMatches(ClimbingCommandRequirement requirement, ClimbingCommand command) {
    if (requirement == ClimbingCommandRequirement::Any) return true;
    return (requirement == ClimbingCommandRequirement::Jump && command == ClimbingCommand::Jump) ||
           (requirement == ClimbingCommandRequirement::Climb && command == ClimbingCommand::Climb) ||
           (requirement == ClimbingCommandRequirement::Drop && command == ClimbingCommand::Drop) ||
           (requirement == ClimbingCommandRequirement::Sprint && command == ClimbingCommand::Sprint) ||
           (requirement == ClimbingCommandRequirement::Crouch && command == ClimbingCommand::Crouch);
}

}  // namespace

eve::Result<ClimbingServiceStart> ClimbingServiceSelectionSystem::tryStart(
    ClimbingRuntime& runtime, physics::World3D& world, const ClimbingPose& pose, ClimbingIntent& intent,
    ClimbingCommand command, eve::SimulationTick tick, eve::SimulationTick lastGroundedTick,
    ClimbingServiceSubject subject) {
    if (subject.isZero())
        return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "service subject must be non-zero", "subject", {}, "climbing.services"));
    if (!ClimbingInputSystem::peek(intent, command, tick))
        return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "climbing.input.no_match", "intent.commands", {}, "climbing.services"));

    ClimbingPose effectivePose = pose;
    if (!effectivePose.grounded && command != ClimbingCommand::Drop &&
        ClimbingInputSystem::coyoteWindowState(tick, lastGroundedTick, runtime.profile_.coyoteTicks) ==
            ClimbingCoyoteState::Eligible)
        effectivePose.grounded = true;
    auto prepared = runtime.prepareBegin(world, effectivePose, tick);
    if (!prepared) return eve::Result<ClimbingServiceStart>::failure(prepared.status());
    if (!commandMatches(prepared.value().action.requiredCommand, command))
        return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "selected action does not accept the buffered command", "action.requiredCommand", {}, "climbing.services"));

    if (!prepared.value().action.requiredConditionTags.empty()) {
        IClimbingConditionAuthority* conditions = eve::cap::query<IClimbingConditionAuthority>();
        if (!conditions)
            return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "required climbing condition provider is absent", "conditions", {}, "climbing.services"));
        auto decision = conditions->evaluate(subject, prepared.value().action.requiredConditionTags, tick);
        if (!decision) return eve::Result<ClimbingServiceStart>::failure(decision.status());
        if (decision.value() != ClimbingConditionDecision::Allowed)
            return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing action conditions were denied", "conditions", {}, "climbing.services"));
        prepared.value().conditionsSatisfied = true;
    }

    IClimbingStaminaAuthority* stamina = nullptr;
    ClimbingStaminaReservation reservation = ClimbingStaminaReservation::zero();
    ClimbingOptionalServiceState staminaState = ClimbingOptionalServiceState::Disabled;
    if (prepared.value().action.staminaCost > 0.f &&
        runtime.profile_.staminaPolicy != ClimbingStaminaPolicy::Disabled) {
        stamina = eve::cap::query<IClimbingStaminaAuthority>();
        if (!stamina && runtime.profile_.staminaPolicy == ClimbingStaminaPolicy::RequireProvider)
            return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "required climbing stamina provider is absent", "stamina", {}, "climbing.services"));
        if (!stamina) {
            staminaState = ClimbingOptionalServiceState::ProviderAbsent;
        } else {
            if (!runtime.profile_.staminaAdapter.empty() &&
                stamina->adapterId() != runtime.profile_.staminaAdapter)
                return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "configured climbing stamina adapter is not available", "staminaAdapter", {}, "climbing.services"));
            auto reserved = stamina->prepare(subject, prepared.value().action.staminaCost, tick);
            if (!reserved) return eve::Result<ClimbingServiceStart>::failure(reserved.status());
            reservation = reserved.value();
            if (reservation.isZero())
                return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "stamina provider returned a zero reservation", "stamina", {}, "climbing.services"));
            staminaState = ClimbingOptionalServiceState::Applied;
        }
    }

    const ClimbingIntent originalIntent = intent;
    auto consumed = ClimbingInputSystem::consume(intent, command, tick, prepared.value().executionId);
    if (!consumed || !consumed.value()) {
        intent = originalIntent;
        if (stamina) stamina->cancelPrepared(reservation);
        if (!consumed) return eve::Result<ClimbingServiceStart>::failure(consumed.status());
        return eve::Result<ClimbingServiceStart>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "eligible climbing input changed before commit", "intent.commands", {}, "climbing.services"));
    }
    auto committed = runtime.commitBegin(std::move(prepared).takeValue());
    if (!committed) {
        intent = originalIntent;
        if (stamina) stamina->cancelPrepared(reservation);
        return eve::Result<ClimbingServiceStart>::failure(committed.status());
    }
    if (stamina) stamina->commitPrepared(reservation, committed.value().executionId);
    return eve::Result<ClimbingServiceStart>::success({std::move(committed).takeValue(), staminaState},
                                                       eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingOptionalServiceState> dispatchClimbingEvents(
    ClimbingServiceSubject subject, std::span<const ClimbingEvent> events) {
    if (subject.isZero())
        return eve::Result<ClimbingOptionalServiceState>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "service subject must be non-zero", "subject", {}, "climbing.services"));
    IClimbingEventSink* sink = eve::cap::query<IClimbingEventSink>();
    if (!sink)
        return eve::Result<ClimbingOptionalServiceState>::success(
            ClimbingOptionalServiceState::ProviderAbsent, eve::Status::success(eve::StatusCode::NoOp));
    auto published = sink->publish(subject, events);
    if (!published) return eve::Result<ClimbingOptionalServiceState>::failure(published.status());
    return eve::Result<ClimbingOptionalServiceState>::success(ClimbingOptionalServiceState::Applied,
                                                               eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingOptionalServiceState> applyClimbingPose(ClimbingServiceSubject subject,
                                                            const ClimbingAdvance& advance) {
    if (subject.isZero())
        return eve::Result<ClimbingOptionalServiceState>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "service subject must be non-zero", "subject", {}, "climbing.services"));
    IClimbingPoseAdapter* adapter = eve::cap::query<IClimbingPoseAdapter>();
    if (!adapter)
        return eve::Result<ClimbingOptionalServiceState>::success(
            ClimbingOptionalServiceState::ProviderAbsent, eve::Status::success(eve::StatusCode::NoOp));
    auto applied = adapter->apply(subject, advance);
    if (!applied) return eve::Result<ClimbingOptionalServiceState>::failure(applied.status());
    return eve::Result<ClimbingOptionalServiceState>::success(ClimbingOptionalServiceState::Applied,
                                                               eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
