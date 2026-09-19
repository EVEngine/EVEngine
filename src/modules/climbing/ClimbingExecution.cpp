#include "climbing/Climbing.h"
#include "climbing/ClimbingRuntimeInternal.h"
#include "climbing/ClimbingTrajectory.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
namespace eve::climbing {
using namespace runtime_detail;
eve::Result<ClimbingAdvance> ClimbingRuntime::advance(physics::World3D& world, eve::SimulationStep step) {
    return advance(world, step, {});
}

eve::Result<ClimbingAdvance> ClimbingRuntime::advance(physics::World3D& world, eve::SimulationStep step,
                                                      const ClimbingMotionInput& motion) {
    lastQueryCount_           = 0;
    lastCounters_             = {};
    lastCounters_.workload    = ClimbingWorkload::Active;
    lastCounters_.queryBudget = ClimbingQueryBudgets::Active;
    RuntimeTelemetryScope telemetryScope(telemetry_, lastCounters_, step.tick);
    if (!isActivePhase(phase_) || !execution_)
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "no climbing execution is active", "runtime.phase", {}, "climbing"));
    Execution& execution       = *execution_;
    lastCounters_.selectedCost = execution.candidate.score;
    if (world.runtimeHandle() != execution.candidate.world)
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "execution belongs to another or stale physics world", "world", {}, "climbing"));
    if (step.tick <= execution.lastTick)
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "simulation tick must increase exactly once per update", "step.tick", {}, "climbing"));
    if (step.delta.nanoseconds() <= 0)
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "simulation delta must be positive", "step.delta", {}, "climbing"));
    if ((motion.rootMotionPolicy != ClimbingRootMotionPolicy::ApplyActionWarp &&
         motion.rootMotionPolicy != ClimbingRootMotionPolicy::PreserveSuppliedDelta) ||
        (motion.obstacleCollision != ClimbingObstacleCollision::Collide &&
         motion.obstacleCollision != ClimbingObstacleCollision::IgnoreTraversedShape) ||
        (!motion.hasRootMotion && (motion.rootMotionPolicy != ClimbingRootMotionPolicy::ApplyActionWarp ||
                                   motion.obstacleCollision != ClimbingObstacleCollision::Collide)))
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "motion policies require valid enum values and authored root motion", "motion.policy", {}, "climbing"));
    if (!isFinite(motion.rootTranslation) || !isFinite(motion.facing) || !isFinite(motion.pelvisOffset) ||
        !isFinite(motion.rootYawRadians) || length(motion.pelvisOffset) > profile_.maxPelvisDeviation + epsilon)
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "authored motion must be finite and pelvis deviation must remain inside the profile limit", "motion", {}, "climbing"));
    if (motion.obstacleCollision == ClimbingObstacleCollision::IgnoreTraversedShape &&
        !world.findShape(execution.candidate.obstacleShape))
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "traversed shape is stale; collision exclusion was not applied", "motion.obstacleCollision", {}, "climbing"));
    auto eventCapacity = requireEventCapacity(4, step.tick);
    if (!eventCapacity) return eve::Result<ClimbingAdvance>::failure(eventCapacity.status());

    const bool graphBound      = execution.anchorGraph.isValid() && !execution.anchorReservation.id.isZero();
    float      graphPointSpeed = -1.f;
    if (graphBound) {
        auto graph    = Climbing::resolveAnchorGraph(execution.anchorGraph);
        auto resolved = graph.isBound() ? graph->resolveNodeKinematics(world, execution.anchorNode)
                                        : eve::Result<ResolvedClimbingAnchorNode>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "anchor graph instance handle is stale", "execution.anchorGraph", {}, "climbing"));
        auto reservation =
            graph.isBound() ? graph->validateReservation(execution.anchorReservation)
                            : eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "anchor graph instance handle is stale", "execution.anchorGraph", {}, "climbing"));
        if (!resolved || !reservation) {
            enqueueEvent(
                {ClimbingEventKind::Cancelled, execution.candidate.actionId, step.tick, execution.executionId});
            phase_        = ClimbingPhase::Cancelled;
            terminalCode_ = "climbing.anchor.stale";
            if (graph.isBound() && reservation) {
                auto released = graph->release(execution.anchorReservation);
                released.ignore("stale graph anchor resolution releases live occupancy");
            }
            execution_.reset();
            return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "graph anchor or occupancy reservation is stale", "execution.anchor", {}, "climbing"));
        }
        execution.candidate.obstacleBody    = resolved.value().body;
        execution.candidate.topPoint        = resolved.value().position;
        execution.candidate.frontPoint      = resolved.value().position;
        execution.candidate.surfaceNormal   = resolved.value().normal;
        execution.candidate.surfaceTangent  = resolved.value().tangent;
        execution.candidate.leftHandAnchor  = resolved.value().leftHandSocket;
        execution.candidate.rightHandAnchor = resolved.value().rightHandSocket;
        if (execution.action.kind == ClimbingActionKind::LadderDismount ||
            execution.action.kind == ClimbingActionKind::BeamBalance) {
            execution.candidate.landingFeet = resolved.value().position;
        } else {
            execution.candidate.landingFeet =
                resolved.value().position +
                resolved.value().normal * (profile_.capsuleRadius + profile_.skin + execution.action.hangBodyOffset);
            execution.candidate.landingFeet.y = resolved.value().position.y - execution.action.hangFeetBelowLedge;
        }
        graphPointSpeed = length(resolved.value().pointVelocity);
    }

    physics::Body3D* obstacle = world.findBody(execution.candidate.obstacleBody);
    if (!obstacle) {
        enqueueEvent({ClimbingEventKind::Cancelled, execution.candidate.actionId, step.tick, execution.executionId});
        phase_        = ClimbingPhase::Cancelled;
        terminalCode_ = "climbing.anchor.stale";
        if (graphBound) {
            auto released = releaseAnchorReservation();
            released.ignore("destroyed anchor body releases graph occupancy");
        }
        execution_.reset();
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "climbing target handle is stale", "execution.candidate.obstacleBody", {}, "climbing"));
    }
    const float platformSpeed = graphPointSpeed >= 0.f
                                    ? graphPointSpeed
                                    : std::sqrt(obstacle->getLinearVelocityX() * obstacle->getLinearVelocityX() +
                                                obstacle->getLinearVelocityY() * obstacle->getLinearVelocityY() +
                                                obstacle->getLinearVelocityZ() * obstacle->getLinearVelocityZ());
    if (platformSpeed > profile_.maxPlatformSpeed + epsilon) {
        enqueueEvent({ClimbingEventKind::Cancelled, execution.candidate.actionId, step.tick, execution.executionId});
        phase_        = ClimbingPhase::Cancelled;
        terminalCode_ = "climbing.anchor.platform_speed";
        if (graphBound) {
            auto released = releaseAnchorReservation();
            released.ignore("unsafe anchor platform speed releases graph occupancy");
        }
        execution_.reset();
        return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing target exceeded the platform speed limit", "execution.candidate.obstacleBody", {}, "climbing"));
    }
    if (!graphBound) {
        auto worldTop = obstacle->localToWorldPointOwned(
            execution.candidate.bodyLocalTop.x, execution.candidate.bodyLocalTop.y, execution.candidate.bodyLocalTop.z);
        if (!worldTop) return eve::Result<ClimbingAdvance>::failure(worldTop.status());
        auto worldLanding = obstacle->localToWorldPointOwned(execution.candidate.bodyLocalLanding.x,
                                                             execution.candidate.bodyLocalLanding.y,
                                                             execution.candidate.bodyLocalLanding.z);
        if (!worldLanding) return eve::Result<ClimbingAdvance>::failure(worldLanding.status());
        execution.candidate.topPoint = {worldTop.value().x, worldTop.value().y, worldTop.value().z};
        if (execution.candidate.kind == ClimbingActionKind::ClimbUp) {
            execution.candidate.landingFeet =
                execution.candidate.topPoint - execution.candidate.surfaceNormal * execution.action.landingForward;
        } else {
            execution.candidate.landingFeet = {worldLanding.value().x, worldLanding.value().y, worldLanding.value().z};
        }
        execution.candidate.leftHandAnchor =
            execution.candidate.topPoint - execution.candidate.surfaceTangent * (execution.action.handSpacing * 0.5f);
        execution.candidate.rightHandAnchor =
            execution.candidate.topPoint + execution.candidate.surfaceTangent * (execution.action.handSpacing * 0.5f);
    }

    physics::QueryFilter3D filter = profile_.queryFilter;
    filter.ignoredBodyId          = execution.candidate.ignoredBodyId;
    if (motion.obstacleCollision == ClimbingObstacleCollision::IgnoreTraversedShape)
        filter.ignoredShapeId = execution.candidate.obstacleShapeId;
    if (phase_ == ClimbingPhase::Hanging || phase_ == ClimbingPhase::Balanced || phase_ == ClimbingPhase::Swinging) {
        const float lowerY = execution.currentFeet.y + profile_.capsuleRadius + profile_.skin;
        const float upperY = execution.currentFeet.y + profile_.capsuleHeight - profile_.capsuleRadius + profile_.skin;
        auto        overlap =
            world.queryCapsuleOwned(execution.currentFeet.x, lowerY, execution.currentFeet.z, execution.currentFeet.x,
                                    upperY, execution.currentFeet.z, profile_.capsuleRadius, filter);
        if (!overlap) return eve::Result<ClimbingAdvance>::failure(overlap.status());
        ++lastQueryCount_;
        lastCounters_.queryCount = lastQueryCount_;
        if (overlap.value().bodyCount != 0) {
            enqueueEvent({ClimbingEventKind::Failed, execution.candidate.actionId, step.tick, execution.executionId});
            phase_        = ClimbingPhase::Failed;
            terminalCode_ = "climbing.candidate.clearance_blocked";
            if (graphBound) {
                auto released = releaseAnchorReservation();
                released.ignore("blocked hanging clearance releases graph occupancy");
            }
            execution_.reset();
            return eve::Result<ClimbingAdvance>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "hanging capsule clearance became blocked", "execution.clearance", {}, "climbing"));
        }
        execution.lastTick = step.tick;
        ClimbingAdvance output{phase_, execution.candidate.actionId, execution.currentFeet, {}, {}, {}, 1.f, false,
                               false,  execution.candidate.support};
        output.leftHandAnchor         = execution.candidate.leftHandAnchor;
        output.rightHandAnchor        = execution.candidate.rightHandAnchor;
        const bool handHold           = phase_ != ClimbingPhase::Balanced;
        const bool authoredContacts   = !execution.action.contactConstraints.empty();
        output.leftHandWeight         = authoredContacts
                                            ? activeContactWeight(execution.action, ClimbingContactTarget::LeftHand, 1.f)
                                            : (handHold ? 1.f : 0.f);
        output.rightHandWeight        = authoredContacts
                                            ? activeContactWeight(execution.action, ClimbingContactTarget::RightHand, 1.f)
                                            : (handHold ? 1.f : 0.f);
        output.leftFootWeight         = activeContactWeight(execution.action, ClimbingContactTarget::LeftFoot, 1.f);
        output.rightFootWeight        = activeContactWeight(execution.action, ClimbingContactTarget::RightFoot, 1.f);
        output.pelvisWeight           = activeContactWeight(execution.action, ClimbingContactTarget::Pelvis, 1.f);
        output.contactWeight          = std::max({output.leftHandWeight, output.rightHandWeight, output.leftFootWeight,
                                                  output.rightFootWeight, output.pelvisWeight});
        output.executionId            = execution.executionId;
        output.compactCollisionActive = execution.compactCollisionActive;
        output.branchWindowOpen       = execution.branchWindowOpen;
        output.cameraCueProfile       = profile_.cameraCueProfile;
        output.cameraCue              = execution.action.cameraCue;
        output.animationClipId        = execution.action.animation.clipId;
        output.animationGraphNodeId   = execution.action.animation.graphNodeId;
        output.animationMirrored      = execution.action.animation.mirrored;
        return eve::Result<ClimbingAdvance>::success(std::move(output));
    }

    if (phase_ == ClimbingPhase::Dropping) {
        const float dt = static_cast<float>(step.delta.seconds());
        execution.velocity.x += world.getGravityX() * dt;
        execution.velocity.y += world.getGravityY() * dt;
        execution.velocity.z += world.getGravityZ() * dt;
        const Vec3  desired = execution.velocity * dt;
        const float lowerY  = execution.currentFeet.y + profile_.capsuleRadius;
        const float upperY  = execution.currentFeet.y + profile_.capsuleHeight - profile_.capsuleRadius;
        auto        moved   = world.moveCapsuleOwned(execution.currentFeet.x, lowerY, execution.currentFeet.z,
                                                     execution.currentFeet.x, upperY, execution.currentFeet.z,
                                                     profile_.capsuleRadius, desired.x, desired.y, desired.z, filter);
        if (!moved) return eve::Result<ClimbingAdvance>::failure(moved.status());
        ++lastQueryCount_;
        lastCounters_.queryCount              = lastQueryCount_;
        const physics::CapsuleMove3D movement = std::move(moved).takeValue();
        lastCounters_.moverIterations         = static_cast<std::uint32_t>(std::max(0, movement.iterations));
        const Vec3 actual{movement.deltaX, movement.deltaY, movement.deltaZ};
        lastCounters_.warpResidual = desired - actual;
        execution.currentFeet      = execution.currentFeet + actual;
        execution.lastTick         = step.tick;
        ClimbingAdvance output{ClimbingPhase::Dropping,
                               execution.candidate.actionId,
                               execution.currentFeet,
                               desired,
                               actual,
                               desired - actual,
                               1.f,
                               movement.constrained,
                               movement.grounded,
                               HangSupport::None};
        output.executionId          = execution.executionId;
        output.cameraCueProfile     = profile_.cameraCueProfile;
        output.cameraCue            = execution.action.cameraCue;
        output.animationClipId      = execution.action.animation.clipId;
        output.animationGraphNodeId = execution.action.animation.graphNodeId;
        output.animationMirrored    = execution.action.animation.mirrored;
        if (movement.grounded) {
            phase_        = ClimbingPhase::Completed;
            terminalCode_ = "climbing.completed";
            output.phase  = phase_;
            enqueueEvent({ClimbingEventKind::Landed, output.actionId, step.tick, execution.executionId});
            enqueueEvent({ClimbingEventKind::Completed, output.actionId, step.tick, execution.executionId});
            output.terminalVelocity    = terminalVelocityFor(execution.action, actual, 1.f / dt);
            output.hasTerminalVelocity = true;
            execution_.reset();
        }
        return eve::Result<ClimbingAdvance>::success(std::move(output));
    }

    auto elapsed = execution.elapsed.tryAdd(step.delta);
    if (!elapsed) return eve::Result<ClimbingAdvance>::failure(elapsed.status());
    execution.elapsed            = std::move(elapsed).takeValue();
    execution.lastTick           = step.tick;
    const double durationSeconds = execution.duration.seconds();
    const float  t = static_cast<float>(std::clamp(execution.elapsed.seconds() / durationSeconds, 0.0, 1.0));
    const Vec3   planned =
        detail::trajectoryPoint(execution.startFeet, execution.candidate, execution.action, profile_, t);
    const Vec3 proceduralDelta = planned - execution.currentFeet;
    Vec3       baseDelta       = proceduralDelta;
    if (motion.hasRootMotion && motion.rootMotionPolicy == ClimbingRootMotionPolicy::PreserveSuppliedDelta) {
        baseDelta = motion.rootTranslation;
    } else if (motion.hasRootMotion) {
        const float authoredLength = length(motion.rootTranslation);
        const float targetLength   = length(proceduralDelta);
        const float scale          = authoredLength > epsilon
                                         ? std::clamp(targetLength / authoredLength, execution.action.rootMotionScaleMin,
                                                      execution.action.rootMotionScaleMax)
                                         : execution.action.rootMotionScaleMin;
        baseDelta                  = motion.rootTranslation * scale;
    }

    const WarpChannels channels  = motion.rootMotionPolicy == ClimbingRootMotionPolicy::PreserveSuppliedDelta
                                       ? WarpChannels{}
                                       : activeWarpChannels(execution.action, t);
    const Vec3         warpError = planned - (execution.currentFeet + baseDelta);
    Vec3               appliedWarp;
    if (channels.horizontal) {
        Vec3        horizontal{warpError.x, 0.f, warpError.z};
        const float remaining = std::max(0.f, execution.action.horizontalWarpBudget - execution.horizontalWarpUsed);
        horizontal    = clampMagnitude(horizontal, std::min(execution.action.maxTranslationWarpPerTick, remaining));
        appliedWarp.x = horizontal.x;
        appliedWarp.z = horizontal.z;
    }
    if (channels.vertical) {
        const float remaining = std::max(0.f, execution.action.verticalWarpBudget - execution.verticalWarpUsed);
        appliedWarp.y = std::clamp(warpError.y, -std::min(execution.action.maxTranslationWarpPerTick, remaining),
                                   std::min(execution.action.maxTranslationWarpPerTick, remaining));
    }
    const float remainingTotalWarp =
        std::max(0.f, profile_.maxTotalWarpBudget - execution.horizontalWarpUsed - execution.verticalWarpUsed);
    appliedWarp = clampMagnitude(appliedWarp, std::min(execution.action.maxTranslationWarpPerTick, remainingTotalWarp));
    execution.horizontalWarpUsed += length({appliedWarp.x, 0.f, appliedWarp.z});
    execution.verticalWarpUsed += std::fabs(appliedWarp.y);
    const Vec3 desired = baseDelta + appliedWarp;

    float desiredYawDelta = motion.rootYawRadians;
    if (channels.facing) {
        const Vec3  targetFacing = execution.candidate.surfaceNormal * -1.f;
        const float yawError     = signedHorizontalAngle(motion.facing, targetFacing) - motion.rootYawRadians;
        const float remaining    = std::max(0.f, execution.action.facingWarpBudgetRadians - execution.facingWarpUsed);
        const float correction   = std::clamp(yawError, -std::min(execution.action.maxYawWarpRadiansPerTick, remaining),
                                              std::min(execution.action.maxYawWarpRadiansPerTick, remaining));
        execution.facingWarpUsed += std::fabs(correction);
        desiredYawDelta += correction;
    }
    const float authoredLeftHand  = activeContactWeight(execution.action, ClimbingContactTarget::LeftHand, t);
    const float authoredRightHand = activeContactWeight(execution.action, ClimbingContactTarget::RightHand, t);
    bool        leftContact       = authoredLeftHand > epsilon;
    bool        rightContact      = authoredRightHand > epsilon;
    bool        landContact       = false;
    bool        compactRequested  = false;
    bool        compactForTick    = execution.compactCollisionActive;
    bool        branchForTick =
        execution.action.branchWindows.empty() ? execution.branchWindowOpen : activeBranchWindow(execution.action, t);
    for (ClimbingNotifyKind notify : motion.notifies) {
        switch (notify) {
            case ClimbingNotifyKind::ContactLeftHand: leftContact = true; break;
            case ClimbingNotifyKind::ContactRightHand: rightContact = true; break;
            case ClimbingNotifyKind::CollisionCompact:
                compactRequested = true;
                compactForTick   = true;
                break;
            case ClimbingNotifyKind::BranchOpen: branchForTick = true; break;
            case ClimbingNotifyKind::BranchClose: branchForTick = false; break;
            case ClimbingNotifyKind::Land:
                landContact    = true;
                compactForTick = false;
                branchForTick  = false;
                break;
        }
    }
    const float collisionHeight = compactForTick ? profile_.compactCapsuleHeight : profile_.capsuleHeight;
    const float lowerY          = execution.currentFeet.y + profile_.capsuleRadius;
    const float upperY          = execution.currentFeet.y + collisionHeight - profile_.capsuleRadius;
    auto        moved           = world.moveCapsuleOwned(execution.currentFeet.x, lowerY, execution.currentFeet.z,
                                                         execution.currentFeet.x, upperY, execution.currentFeet.z,
                                                         profile_.capsuleRadius, desired.x, desired.y, desired.z, filter);
    if (!moved) return eve::Result<ClimbingAdvance>::failure(moved.status());
    ++lastQueryCount_;
    lastCounters_.queryCount              = lastQueryCount_;
    const physics::CapsuleMove3D movement = std::move(moved).takeValue();
    lastCounters_.moverIterations         = static_cast<std::uint32_t>(std::max(0, movement.iterations));
    const Vec3 actual{movement.deltaX, movement.deltaY, movement.deltaZ};
    execution.currentFeet            = execution.currentFeet + actual;
    execution.lastPlannedFeet        = planned;
    const Vec3 residual              = desired - actual;
    lastCounters_.warpResidual       = residual;
    execution.accumulatedResidual    = execution.accumulatedResidual + residual;
    execution.compactCollisionActive = compactForTick;
    execution.branchWindowOpen       = branchForTick;
    if (debugCapture_ == ClimbingDebugCapture::Enabled)
        boundedDebugPush(motionEvidence_, ClimbingMotionEvidence{step.tick, planned, execution.currentFeet, residual,
                                                                 collisionHeight, movement.constrained});
    ClimbingAdvance output{ClimbingPhase::Climbing,
                           execution.candidate.actionId,
                           execution.currentFeet,
                           desired,
                           actual,
                           residual,
                           t,
                           movement.constrained,
                           movement.grounded,
                           execution.candidate.support};
    output.executionId          = execution.executionId;
    output.appliedWarp          = appliedWarp;
    output.desiredYawDelta      = desiredYawDelta;
    output.cameraCueProfile     = profile_.cameraCueProfile;
    output.cameraCue            = execution.action.cameraCue;
    output.animationClipId      = execution.action.animation.clipId;
    output.animationGraphNodeId = execution.action.animation.graphNodeId;
    output.animationMirrored    = execution.action.animation.mirrored;
    output.leftHandAnchor       = execution.candidate.leftHandAnchor;
    output.rightHandAnchor      = execution.candidate.rightHandAnchor;
    if (leftContact && !execution.leftContactEmitted) {
        execution.leftContactEmitted = true;
        enqueueEvent({ClimbingEventKind::ContactLeftHand, output.actionId, step.tick, execution.executionId});
    }
    if (rightContact && !execution.rightContactEmitted) {
        execution.rightContactEmitted = true;
        enqueueEvent({ClimbingEventKind::ContactRightHand, output.actionId, step.tick, execution.executionId});
    }
    if (landContact) execution.landContactReleased = true;
    if (execution.action.contactConstraints.empty()) {
        output.leftHandWeight  = execution.leftContactEmitted && !execution.landContactReleased ? 1.f : 0.f;
        output.rightHandWeight = execution.rightContactEmitted && !execution.landContactReleased ? 1.f : 0.f;
    } else if (!execution.landContactReleased) {
        output.leftHandWeight  = authoredLeftHand;
        output.rightHandWeight = authoredRightHand;
        output.leftFootWeight  = activeContactWeight(execution.action, ClimbingContactTarget::LeftFoot, t);
        output.rightFootWeight = activeContactWeight(execution.action, ClimbingContactTarget::RightFoot, t);
        output.pelvisWeight    = activeContactWeight(execution.action, ClimbingContactTarget::Pelvis, t);
    }
    output.contactWeight             = std::max({output.leftHandWeight, output.rightHandWeight, output.leftFootWeight,
                                                 output.rightFootWeight, output.pelvisWeight});
    output.compactCollisionRequested = compactRequested;
    output.compactCollisionActive    = execution.compactCollisionActive;
    output.branchWindowOpen          = execution.branchWindowOpen;
    if (const std::string* comboTag = activeBranchComboTag(execution.action, t)) output.branchComboTag = *comboTag;
    if (t < 0.1f)
        phase_ = ClimbingPhase::Aligning;
    else if (t < 0.2f)
        phase_ = ClimbingPhase::Launching;
    else if (t < 0.8f)
        phase_ = ClimbingPhase::Climbing;
    else if (t < 0.95f)
        phase_ = ClimbingPhase::Landing;
    else
        phase_ = ClimbingPhase::Recovering;
    output.phase                    = phase_;
    const Vec3 remainingTargetError = planned - execution.currentFeet;
    const bool authoredWarpMissed   = motion.hasRootMotion &&
                                    motion.rootMotionPolicy == ClimbingRootMotionPolicy::ApplyActionWarp &&
                                    t + epsilon >= finalWarpWindowEnd(execution.action) &&
                                    length(remainingTargetError) > profile_.maxWarpResidual + epsilon;
    if (length(execution.accumulatedResidual) > profile_.maxWarpResidual + epsilon || authoredWarpMissed) {
        phase_        = ClimbingPhase::Failed;
        terminalCode_ = "climbing.warp.budget_exceeded";
        output.phase  = phase_;
        enqueueEvent({ClimbingEventKind::Failed, output.actionId, step.tick, execution.executionId});
        if (graphBound) {
            auto released = releaseAnchorReservation();
            released.ignore("failed anchor motion releases graph occupancy");
        }
        execution_.reset();
    } else if (t >= 1.f - epsilon) {
        if (graphBound && isAnchorHangEnd(execution.candidate.kind)) {
            if (execution.candidate.kind == ClimbingActionKind::BeamBalance)
                phase_ = ClimbingPhase::Balanced;
            else if (execution.candidate.kind == ClimbingActionKind::PoleSwing ||
                     execution.candidate.kind == ClimbingActionKind::BarSwing)
                phase_ = ClimbingPhase::Swinging;
            else
                phase_ = ClimbingPhase::Hanging;
            execution.branchWindowOpen = true;
            output.branchWindowOpen    = true;
            enqueueEvent({execution.candidate.kind == ClimbingActionKind::LedgeGrab ? ClimbingEventKind::Hanging
                                                                                    : ClimbingEventKind::AnchorReached,
                          output.actionId, step.tick, execution.executionId});
        } else if (execution.candidate.kind == ClimbingActionKind::LedgeGrab) {
            phase_                     = ClimbingPhase::Hanging;
            execution.branchWindowOpen = true;
            output.branchWindowOpen    = true;
            enqueueEvent({ClimbingEventKind::Hanging, output.actionId, step.tick, execution.executionId});
        } else {
            phase_        = ClimbingPhase::Completed;
            terminalCode_ = "climbing.completed";
            if (execution.candidate.kind != ClimbingActionKind::WallRun)
                enqueueEvent({ClimbingEventKind::Landed, output.actionId, step.tick, execution.executionId});
            enqueueEvent({ClimbingEventKind::Completed, output.actionId, step.tick, execution.executionId});
            if (graphBound) {
                auto released = releaseAnchorReservation();
                released.ignore("completed graph dismount releases occupancy");
            }
        }
        output.phase = phase_;
        if (phase_ == ClimbingPhase::Completed) {
            output.terminalVelocity =
                terminalVelocityFor(execution.action, actual, static_cast<float>(1.0 / step.delta.seconds()));
            output.hasTerminalVelocity = true;
            execution_.reset();
        }
    }
    return eve::Result<ClimbingAdvance>::success(std::move(output));
}


}  // namespace eve::climbing
