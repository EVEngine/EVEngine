#include "combat/CombatLocomotion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::combat {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool finite(CombatVector2 value) { return std::isfinite(value.x) && std::isfinite(value.z); }

double length(CombatVector2 value) { return std::hypot(value.x, value.z); }

CombatVector2 normalized(CombatVector2 value) {
    const double magnitude = length(value);
    return magnitude > 0.0 ? CombatVector2{value.x / magnitude, value.z / magnitude} : CombatVector2{};
}

Result<void> validateGoal(const CombatNavigationGoal& goal) {
    if (!finite(goal.position) || !std::isfinite(goal.acceptanceRadius) || goal.acceptanceRadius < 0.0)
        return failure<void>(DiagnosticCode::InvalidArgument, "navigation goal is invalid", "goal");
    return Result<void>::success();
}

Result<void> validateSteering(const CombatNavigationSteering& steering) {
    if ((steering.phase != CombatNavigationPhase::Moving &&
         steering.phase != CombatNavigationPhase::Arrived) ||
        !finite(steering.direction) || !std::isfinite(steering.speedFraction) ||
        steering.speedFraction < 0.0 || steering.speedFraction > 1.0)
        return failure<void>(DiagnosticCode::InvalidArgument, "navigation provider returned invalid steering",
                             "steering");
    return Result<void>::success();
}

}  // namespace

Result<void> CombatLocomotionDefinition::validate() const {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "subject is nil", "subject");
    if (ownerId.empty()) return failure<void>(DiagnosticCode::InvalidArgument, "owner id is empty", "ownerId");
    if (!finite(initialPosition) || !std::isfinite(maximumSpeed) || maximumSpeed <= 0.0 ||
        !std::isfinite(acceleration) || acceleration <= 0.0)
        return failure<void>(DiagnosticCode::InvalidArgument, "locomotion limits are invalid", "movement");
    return Result<void>::success();
}

Result<CombatNavigationSteering> DirectCombatNavigationProvider::steer(
    const CombatLocomotionState& state, const CombatNavigationGoal& goal, SimulationTick tick) {
    (void)tick;
    auto valid = validateGoal(goal);
    if (!valid) return Result<CombatNavigationSteering>::failure(valid.status());
    const CombatVector2 delta{goal.position.x - state.position.x, goal.position.z - state.position.z};
    const double distance = length(delta);
    if (distance <= goal.acceptanceRadius)
        return Result<CombatNavigationSteering>::success(
            {CombatNavigationPhase::Arrived, {}, 0.0});
    return Result<CombatNavigationSteering>::success(
        {CombatNavigationPhase::Moving, normalized(delta), 1.0});
}

Result<void> CombatLocomotionRuntime::registerSubject(CombatLocomotionDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return valid;
    const std::string key = definition.subject.format();
    if (states_.contains(key))
        return failure<void>(DiagnosticCode::AlreadyExists, "locomotion subject already exists", key);
    states_.emplace(key, CombatLocomotionState{definition.subject, std::move(definition.ownerId),
                                               definition.initialPosition, {}, {1.0, 0.0}, {}, 0.0,
                                               definition.maximumSpeed, definition.acceleration, std::nullopt});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::unregisterSubject(SubjectRef subject) {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "subject is nil", "subject");
    if (states_.erase(subject.format()) == 0)
        return failure<void>(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format());
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLocomotionState> CombatLocomotionRuntime::state(SubjectRef subject) const {
    if (!subject.isValid())
        return failure<CombatLocomotionState>(DiagnosticCode::InvalidArgument, "subject is nil", "subject");
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return failure<CombatLocomotionState>(DiagnosticCode::NotFound, "locomotion subject was not found",
                                              subject.format());
    return Result<CombatLocomotionState>::success(found->second);
}

Result<void> CombatLocomotionRuntime::setMoveIntent(SubjectRef subject, CombatVector2 direction,
                                                     double speedFraction) {
    if (!subject.isValid() || !finite(direction) || !std::isfinite(speedFraction) || speedFraction < 0.0 ||
        speedFraction > 1.0)
        return failure<void>(DiagnosticCode::InvalidArgument, "movement intent is invalid", "intent");
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return failure<void>(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format());
    found->second.moveDirection = normalized(direction);
    found->second.moveSpeedFraction = length(direction) > 0.0 ? speedFraction : 0.0;
    found->second.navigationGoal.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::navigateTo(SubjectRef subject, CombatNavigationGoal goal) {
    if (!subject.isValid()) return failure<void>(DiagnosticCode::InvalidArgument, "subject is nil", "subject");
    if (!navigation_)
        return failure<void>(DiagnosticCode::Unsupported, "combat navigation provider is unavailable", "navigation");
    auto valid = validateGoal(goal);
    if (!valid) return valid;
    const auto found = states_.find(subject.format());
    if (found == states_.end())
        return failure<void>(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format());
    found->second.navigationGoal = goal;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> CombatLocomotionRuntime::stop(SubjectRef subject) {
    const auto found = states_.find(subject.format());
    if (!subject.isValid() || found == states_.end())
        return failure<void>(DiagnosticCode::NotFound, "locomotion subject was not found", subject.format());
    found->second.moveDirection = {};
    found->second.moveSpeedFraction = 0.0;
    found->second.navigationGoal.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<CombatLocomotionAdvance> CombatLocomotionRuntime::advance(const SimulationStep& step) {
    if (step.delta < Duration::zero() || step.tick < lastTick_)
        return failure<CombatLocomotionAdvance>(DiagnosticCode::InvalidArgument,
                                                "locomotion step is negative or moves tick backwards", "step");
    auto candidate = states_;
    CombatLocomotionAdvance result;
    result.tick = step.tick;
    const double seconds = step.delta.seconds();
    for (auto& [key, state] : candidate) {
        (void)key;
        bool arrived = false;
        std::optional<CombatNavigationGoal> activeGoal = state.navigationGoal;
        if (state.navigationGoal) {
            if (!navigation_)
                return failure<CombatLocomotionAdvance>(DiagnosticCode::Unsupported,
                                                        "active goal has no combat navigation provider",
                                                        state.subject.format());
            auto steering = navigation_->steer(state, *state.navigationGoal, step.tick);
            if (!steering) return Result<CombatLocomotionAdvance>::failure(steering.status());
            auto valid = validateSteering(steering.value());
            if (!valid) return Result<CombatLocomotionAdvance>::failure(valid.status());
            arrived = steering.value().phase == CombatNavigationPhase::Arrived;
            state.moveDirection = arrived ? CombatVector2{} : normalized(steering.value().direction);
            state.moveSpeedFraction = arrived ? 0.0 : steering.value().speedFraction;
            if (arrived) {
                state.navigationGoal.reset();
                state.velocity = {};
            }
        }
        const CombatVector2 previous = state.position;
        const CombatVector2 desired{state.moveDirection.x * state.maximumSpeed * state.moveSpeedFraction,
                                    state.moveDirection.z * state.maximumSpeed * state.moveSpeedFraction};
        const CombatVector2 velocityDelta{desired.x - state.velocity.x, desired.z - state.velocity.z};
        const double deltaLength = length(velocityDelta);
        const double maximumDelta = state.acceleration * seconds;
        const double scale = deltaLength > maximumDelta && deltaLength > 0.0 ? maximumDelta / deltaLength : 1.0;
        state.velocity.x += velocityDelta.x * scale;
        state.velocity.z += velocityDelta.z * scale;
        state.position.x += state.velocity.x * seconds;
        state.position.z += state.velocity.z * seconds;
        if (activeGoal && !arrived) {
            const CombatVector2 before{activeGoal->position.x - previous.x,
                                       activeGoal->position.z - previous.z};
            const CombatVector2 after{activeGoal->position.x - state.position.x,
                                      activeGoal->position.z - state.position.z};
            if (length(after) <= activeGoal->acceptanceRadius || before.x * after.x + before.z * after.z <= 0.0) {
                state.position = activeGoal->position;
                state.velocity = {};
                state.moveDirection = {};
                state.moveSpeedFraction = 0.0;
                state.navigationGoal.reset();
                arrived = true;
            }
        }
        if (length(state.velocity) > 0.0) state.facing = normalized(state.velocity);
        if (state.position.x != previous.x || state.position.z != previous.z)
            result.events.push_back({state.subject, CombatLocomotionEventKind::Moved, previous, state.position,
                                     step.tick});
        if (arrived)
            result.events.push_back({state.subject, CombatLocomotionEventKind::Arrived, previous, state.position,
                                     step.tick});
    }
    states_ = std::move(candidate);
    lastTick_ = step.tick;
    return Result<CombatLocomotionAdvance>::success(std::move(result), Status::success(StatusCode::Applied));
}

std::vector<CombatLocomotionState> CombatLocomotionRuntime::states() const {
    std::vector<CombatLocomotionState> result;
    result.reserve(states_.size());
    for (const auto& [key, state] : states_) {
        (void)key;
        result.push_back(state);
    }
    return result;
}

}  // namespace eve::combat
