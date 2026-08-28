#include "climbing/ClimbingServices.h"

#include "common/Capability.h"
#include "physics/World3D.h"

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.services"));
}

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
        return failure<ClimbingServiceStart>(eve::DiagnosticCode::InvalidArgument,
                                             "service subject must be non-zero", "subject");
    if (!ClimbingInputSystem::peek(intent, command, tick))
        return failure<ClimbingServiceStart>(eve::DiagnosticCode::NotFound, "climbing.input.no_match",
                                             "intent.commands");

    ClimbingPose effectivePose = pose;
    if (!effectivePose.grounded && command != ClimbingCommand::Drop &&
        ClimbingInputSystem::coyoteWindowState(tick, lastGroundedTick, runtime.profile_.coyoteTicks) ==
            ClimbingCoyoteState::Eligible)
        effectivePose.grounded = true;
    auto prepared = runtime.prepareBegin(world, effectivePose, tick);
    if (!prepared) return eve::Result<ClimbingServiceStart>::failure(prepared.status());
    if (!commandMatches(prepared.value().action.requiredCommand, command))
        return failure<ClimbingServiceStart>(eve::DiagnosticCode::NotFound,
                                             "selected action does not accept the buffered command",
                                             "action.requiredCommand");

    if (!prepared.value().action.requiredConditionTags.empty()) {
        IClimbingConditionAuthority* conditions = eve::cap::query<IClimbingConditionAuthority>();
        if (!conditions)
            return failure<ClimbingServiceStart>(eve::DiagnosticCode::Unsupported,
                                                 "required climbing condition provider is absent", "conditions");
        auto decision = conditions->evaluate(subject, prepared.value().action.requiredConditionTags, tick);
        if (!decision) return eve::Result<ClimbingServiceStart>::failure(decision.status());
        if (decision.value() != ClimbingConditionDecision::Allowed)
            return failure<ClimbingServiceStart>(eve::DiagnosticCode::PreconditionViolation,
                                                 "climbing action conditions were denied", "conditions");
        prepared.value().conditionsSatisfied = true;
    }

    IClimbingStaminaAuthority* stamina = nullptr;
    ClimbingStaminaReservation reservation = ClimbingStaminaReservation::zero();
    ClimbingOptionalServiceState staminaState = ClimbingOptionalServiceState::Disabled;
    if (prepared.value().action.staminaCost > 0.f &&
        runtime.profile_.staminaPolicy != ClimbingStaminaPolicy::Disabled) {
        stamina = eve::cap::query<IClimbingStaminaAuthority>();
        if (!stamina && runtime.profile_.staminaPolicy == ClimbingStaminaPolicy::RequireProvider)
            return failure<ClimbingServiceStart>(eve::DiagnosticCode::Unsupported,
                                                 "required climbing stamina provider is absent", "stamina");
        if (!stamina) {
            staminaState = ClimbingOptionalServiceState::ProviderAbsent;
        } else {
            if (!runtime.profile_.staminaAdapter.empty() &&
                stamina->adapterId() != runtime.profile_.staminaAdapter)
                return failure<ClimbingServiceStart>(eve::DiagnosticCode::Unsupported,
                                                     "configured climbing stamina adapter is not available",
                                                     "staminaAdapter");
            auto reserved = stamina->prepare(subject, prepared.value().action.staminaCost, tick);
            if (!reserved) return eve::Result<ClimbingServiceStart>::failure(reserved.status());
            reservation = reserved.value();
            if (reservation.isZero())
                return failure<ClimbingServiceStart>(eve::DiagnosticCode::InvariantViolation,
                                                     "stamina provider returned a zero reservation", "stamina");
            staminaState = ClimbingOptionalServiceState::Applied;
        }
    }

    const ClimbingIntent originalIntent = intent;
    auto consumed = ClimbingInputSystem::consume(intent, command, tick, prepared.value().executionId);
    if (!consumed || !consumed.value()) {
        intent = originalIntent;
        if (stamina) stamina->cancelPrepared(reservation);
        if (!consumed) return eve::Result<ClimbingServiceStart>::failure(consumed.status());
        return failure<ClimbingServiceStart>(eve::DiagnosticCode::Conflict,
                                             "eligible climbing input changed before commit", "intent.commands");
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
        return failure<ClimbingOptionalServiceState>(eve::DiagnosticCode::InvalidArgument,
                                                      "service subject must be non-zero", "subject");
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
        return failure<ClimbingOptionalServiceState>(eve::DiagnosticCode::InvalidArgument,
                                                      "service subject must be non-zero", "subject");
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
