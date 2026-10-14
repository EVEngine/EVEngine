#include "climbing/ClimbingInput.h"
#include "climbing/ClimbingTrajectory.h"

#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::climbing {
namespace {

bool activePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed && phase != ClimbingPhase::Cancelled &&
           phase != ClimbingPhase::Failed;
}

bool commandMatches(ClimbingCommandRequirement requirement, ClimbingCommand command) {
    if (requirement == ClimbingCommandRequirement::Any) return true;
    switch (command) {
        case ClimbingCommand::Jump: return requirement == ClimbingCommandRequirement::Jump;
        case ClimbingCommand::Climb: return requirement == ClimbingCommandRequirement::Climb;
        case ClimbingCommand::Drop: return requirement == ClimbingCommandRequirement::Drop;
        case ClimbingCommand::Sprint: return requirement == ClimbingCommandRequirement::Sprint;
        case ClimbingCommand::Crouch: return requirement == ClimbingCommandRequirement::Crouch;
    }
    return false;
}

const ClimbingActionDefinition* findAction(const ClimbingProfileDefinition& profile, std::string_view id) {
    for (const ClimbingActionDefinition& action : profile.actions)
        if (action.id == id) return &action;
    return nullptr;
}

Vec3  subtract(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
float length(Vec3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }

eve::Result<void> validateFullCapsulePath(physics::World3D& world, Vec3 start, const ClimbingCandidate& candidate,
                                          const ClimbingActionDefinition&  action,
                                          const ClimbingProfileDefinition& profile, std::uint32_t& queryCount,
                                          std::uint32_t queryBudget, bool& budgetExceeded,
                                          std::uint32_t& moverIterations) {
    physics::QueryFilter3D filter = profile.queryFilter;
    filter.ignoredBodyId          = candidate.ignoredBodyId;
    Vec3 current                  = start;
    const std::uint32_t remainingBudget = queryCount < queryBudget ? queryBudget - queryCount : 0;
    const std::uint32_t segmentCount = std::min(profile.pathValidationSegments, remainingBudget);
    if (segmentCount == 0) {
        budgetExceeded = true;
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "climbing.query_budget.exceeded", "candidate.path", {},
                                                                 "climbing.selection"));
    }
    if (segmentCount < profile.pathValidationSegments) budgetExceeded = true;
    for (std::uint32_t segment = 1; segment <= segmentCount; ++segment) {
        const float t       = static_cast<float>(segment) / static_cast<float>(segmentCount);
        const Vec3  target  = detail::trajectoryPoint(start, candidate, action, profile, t);
        const Vec3  desired = subtract(target, current);
        const float lowerY  = current.y + profile.capsuleRadius;
        const float collisionHeight = action.kind == ClimbingActionKind::Slide
                                          ? profile.compactCapsuleHeight
                                          : profile.capsuleHeight;
        const float upperY  = current.y + collisionHeight - profile.capsuleRadius;
        auto        moved   = world.moveCapsuleOwned(current.x, lowerY, current.z, current.x, upperY, current.z,
                                                     profile.capsuleRadius, desired.x, desired.y, desired.z, filter);
        ++queryCount;
        if (!moved) return eve::Result<void>::failure(moved.status());
        moverIterations += static_cast<std::uint32_t>(std::max(0, moved.value().iterations));
        const Vec3 actual{moved.value().deltaX, moved.value().deltaY, moved.value().deltaZ};
        if (length(subtract(desired, actual)) > profile.skin + 0.001f)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing.candidate.path_blocked",
                                       "candidate.path." + std::to_string(segment), {}, "climbing.selection"));
        current = target;
    }
    return eve::Result<void>::success();
}

}  // namespace

eve::Result<ClimbingRuntime::PreparedBegin> ClimbingRuntime::prepareBegin(physics::World3D&   world,
                                                                          const ClimbingPose& pose,
                                                                          eve::SimulationTick tick) {
    if (activePhase(phase_))
        return eve::Result<PreparedBegin>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                          "a climbing execution is already active",
                                                                          "runtime.phase", {}, "climbing.selection"));
    if (nextExecutionId_ == 0 || nextExecutionId_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<PreparedBegin>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "climbing execution id space is exhausted",
            "runtime.nextExecutionId", {}, "climbing.selection"));

    auto candidates = probe(world, pose);
    if (!candidates) return eve::Result<PreparedBegin>::failure(candidates.status());
    ClimbingCandidateSet values = std::move(candidates).takeValue();
    if (values.empty())
        return eve::Result<PreparedBegin>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                          "no valid climbing candidate was found",
                                                                          "candidate", {}, "climbing.selection"));
    return prepareBeginCandidate(world, pose, tick, values.takeFront());
}

eve::Result<ClimbingRuntime::PreparedBegin> ClimbingRuntime::prepareBeginCandidate(
    physics::World3D& world, const ClimbingPose& pose, eve::SimulationTick tick, ClimbingCandidate candidate) {
    if (activePhase(phase_))
        return eve::Result<PreparedBegin>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                          "a climbing execution is already active",
                                                                          "runtime.phase", {}, "climbing.selection"));
    if (nextExecutionId_ == 0 || nextExecutionId_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<PreparedBegin>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "climbing execution id space is exhausted",
            "runtime.nextExecutionId", {}, "climbing.selection"));
    const ClimbingActionDefinition* action    = findAction(profile_, candidate.actionId);
    if (!action)
        return eve::Result<PreparedBegin>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "selected action definition disappeared",
                                   "candidate.actionId", {}, "climbing.selection"));
    if (!action->requiredNotifies.empty() && !validatedAnimationActions_.contains(action->id))
        return eve::Result<PreparedBegin>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "climbing.animation.notify_missing: action clip contract was not validated",
                                   "candidate.actionId", {}, "climbing.selection"));
    bool budgetExceeded = false;
    std::uint32_t moverIterations = 0;
    auto path = validateFullCapsulePath(world, pose.feet, candidate, *action, profile_, lastQueryCount_,
                                        ClimbingQueryBudgets::CandidateProbe, budgetExceeded, moverIterations);
    lastCounters_.queryCount = lastQueryCount_;
    lastCounters_.moverIterations += moverIterations;
    if (budgetExceeded) lastCounters_.budgetState = ClimbingQueryBudgetState::Exceeded;
    telemetry_.updateLatestCounters(lastCounters_);
    if (!path) return eve::Result<PreparedBegin>::failure(path.status());

    PreparedBegin prepared;
    prepared.executionId = ClimbingExecutionId(nextExecutionId_);
    prepared.candidate   = std::move(candidate);
    prepared.action      = *action;
    prepared.startFeet   = pose.feet;
    prepared.tick        = tick;
    prepared.conditionsSatisfied = action->requiredConditionTags.empty();
    return eve::Result<PreparedBegin>::success(std::move(prepared));
}

eve::Result<ClimbingStart> ClimbingRuntime::commitBegin(PreparedBegin prepared) {
    if (activePhase(phase_) || execution_)
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                          "climbing state changed before commit",
                                                                          "runtime.phase", {}, "climbing.selection"));
    if (prepared.executionId.isZero() || prepared.executionId.value() != nextExecutionId_)
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                          "prepared climbing execution is stale",
                                                                          "executionId", {}, "climbing.selection"));
    if (!prepared.conditionsSatisfied)
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "required climbing conditions were not evaluated by a service authority",
            "action.requiredConditionTags", {}, "climbing.selection"));
    auto eventCapacity = requireEventCapacity(1, prepared.tick);
    if (!eventCapacity) return eve::Result<ClimbingStart>::failure(eventCapacity.status());

    Execution execution;
    execution.executionId     = prepared.executionId;
    execution.definitionGeneration = prepared.candidate.definitionGeneration;
    execution.candidate       = prepared.candidate;
    execution.action          = std::move(prepared.action);
    execution.startFeet       = prepared.startFeet;
    execution.currentFeet     = prepared.startFeet;
    execution.lastPlannedFeet = prepared.startFeet;
    execution.duration        = execution.action.duration;
    execution.lastTick        = prepared.tick;

    ClimbingStart started{prepared.executionId, prepared.candidate};
    execution_ = std::move(execution);
    previousActionId_ = started.candidate.actionId;
    if (debugCapture_ == ClimbingDebugCapture::Enabled) motionEvidence_.clear();
    phase_     = ClimbingPhase::Requested;
    terminalCode_.clear();
    ++nextExecutionId_;
    enqueueEvent({ClimbingEventKind::Started, started.candidate.actionId, prepared.tick, started.executionId});
    return eve::Result<ClimbingStart>::success(std::move(started), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingCandidate> ClimbingRuntime::tryBegin(physics::World3D& world, const ClimbingPose& pose,
                                                         eve::SimulationTick tick) {
    auto prepared = prepareBegin(world, pose, tick);
    if (!prepared) return eve::Result<ClimbingCandidate>::failure(prepared.status());
    auto committed = commitBegin(std::move(prepared).takeValue());
    if (!committed) return eve::Result<ClimbingCandidate>::failure(committed.status());
    return eve::Result<ClimbingCandidate>::success(std::move(committed).takeValue().candidate,
                                                   eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingStart> ClimbingSelectionSystem::tryStart(ClimbingRuntime& runtime, physics::World3D& world,
                                                             const ClimbingPose& pose, ClimbingIntent& intent,
                                                             ClimbingCommand command, eve::SimulationTick tick,
                                                             eve::SimulationTick lastGroundedTick) {
    if (!ClimbingInputSystem::peek(intent, command, tick))
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "climbing.input.no_match", "intent.commands", {}, "climbing.selection"));

    ClimbingPose effectivePose = pose;
    if (!effectivePose.grounded && command != ClimbingCommand::Drop &&
        ClimbingInputSystem::coyoteWindowState(tick, lastGroundedTick, runtime.profile_.coyoteTicks) ==
            ClimbingCoyoteState::Eligible)
        effectivePose.grounded = true;

    auto prepared = runtime.prepareBegin(world, effectivePose, tick);
    if (!prepared) return eve::Result<ClimbingStart>::failure(prepared.status());
    if (!commandMatches(prepared.value().action.requiredCommand, command))
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "selected climbing action does not accept the buffered command",
            "action.requiredCommand", {}, "climbing.selection"));

    const ClimbingIntent originalIntent = intent;
    auto                 consumed = ClimbingInputSystem::consume(intent, command, tick, prepared.value().executionId);
    if (!consumed || !consumed.value()) {
        intent = originalIntent;
        if (!consumed) return eve::Result<ClimbingStart>::failure(consumed.status());
        return eve::Result<ClimbingStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "eligible climbing input changed before transaction commit",
            "intent.commands", {}, "climbing.selection"));
    }

    auto committed = runtime.commitBegin(std::move(prepared).takeValue());
    if (!committed) {
        intent = originalIntent;
        return eve::Result<ClimbingStart>::failure(committed.status());
    }
    return committed;
}

}  // namespace eve::climbing
