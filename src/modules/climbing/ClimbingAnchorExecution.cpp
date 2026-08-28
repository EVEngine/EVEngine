#include "climbing/Climbing.h"

#include "climbing/ClimbingTrajectory.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::climbing {
namespace {

constexpr float epsilon = 1e-5f;

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.anchor_execution"));
}

Vec3 subtract(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
Vec3 add(Vec3 lhs, Vec3 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
Vec3 scale(Vec3 value, float amount) { return {value.x * amount, value.y * amount, value.z * amount}; }
float length(Vec3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }

bool activePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed && phase != ClimbingPhase::Cancelled &&
           phase != ClimbingPhase::Failed;
}

const ClimbingActionDefinition* findAction(const ClimbingProfileDefinition& profile, std::string_view id) {
    const auto found = std::lower_bound(profile.actions.begin(), profile.actions.end(), id,
                                        [](const auto& action, std::string_view value) { return action.id < value; });
    return found != profile.actions.end() && found->id == id ? &*found : nullptr;
}

bool startKindMatches(ClimbingAnchorKind nodeKind, ClimbingActionKind actionKind) {
    if (nodeKind == ClimbingAnchorKind::LadderRung)
        return actionKind == ClimbingActionKind::LadderMount || actionKind == ClimbingActionKind::LedgeGrab;
    if (nodeKind == ClimbingAnchorKind::Beam) return actionKind == ClimbingActionKind::BeamBalance;
    if (nodeKind == ClimbingAnchorKind::Pole) return actionKind == ClimbingActionKind::PoleSwing;
    if (nodeKind == ClimbingAnchorKind::Bar) return actionKind == ClimbingActionKind::BarSwing;
    return actionKind == ClimbingActionKind::LedgeGrab || actionKind == ClimbingActionKind::ClimbDown;
}

bool edgeKindMatches(ClimbingAnchorEdgeKind edgeKind, ClimbingActionKind actionKind) {
    switch (edgeKind) {
        case ClimbingAnchorEdgeKind::Shimmy: return actionKind == ClimbingActionKind::Shimmy;
        case ClimbingAnchorEdgeKind::Corner:
            return actionKind == ClimbingActionKind::CornerInner || actionKind == ClimbingActionKind::CornerOuter;
        case ClimbingAnchorEdgeKind::Jump: return actionKind == ClimbingActionKind::LedgeJump;
        case ClimbingAnchorEdgeKind::Drop: return actionKind == ClimbingActionKind::ClimbDown;
        case ClimbingAnchorEdgeKind::Mount: return actionKind == ClimbingActionKind::LadderMount;
        case ClimbingAnchorEdgeKind::Dismount: return actionKind == ClimbingActionKind::LadderDismount;
        case ClimbingAnchorEdgeKind::Climb: return actionKind == ClimbingActionKind::LadderClimb;
        case ClimbingAnchorEdgeKind::Balance: return actionKind == ClimbingActionKind::BeamBalance;
        case ClimbingAnchorEdgeKind::Swing:
            return actionKind == ClimbingActionKind::PoleSwing || actionKind == ClimbingActionKind::BarSwing;
    }
    return false;
}

bool tagsContain(const std::vector<std::string>& values, const std::vector<std::string>& required) {
    return std::all_of(required.begin(), required.end(), [&](const auto& tag) {
        return std::find(values.begin(), values.end(), tag) != values.end();
    });
}

Vec3 hangingFeet(const ResolvedClimbingAnchorNode& node, const ClimbingActionDefinition& action,
                 const ClimbingProfileDefinition& profile) {
    Vec3 result = add(node.position,
                      scale(node.normal, profile.capsuleRadius + profile.skin + action.hangBodyOffset));
    result.y = node.position.y - action.hangFeetBelowLedge;
    return result;
}

eve::Result<void> validatePath(physics::World3D& world, Vec3 start, const ClimbingCandidate& candidate,
                               const ClimbingActionDefinition& action, const ClimbingProfileDefinition& profile,
                               std::uint32_t& queryCount) {
    physics::QueryFilter3D filter = profile.queryFilter;
    filter.ignoredBodyId = candidate.ignoredBodyId;
    Vec3 current = start;
    for (std::uint32_t segment = 1; segment <= profile.pathValidationSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(profile.pathValidationSegments);
        const Vec3 target = detail::trajectoryPoint(start, candidate, action, profile, t);
        const Vec3 desired = subtract(target, current);
        const float lowerY = current.y + profile.capsuleRadius;
        const float upperY = current.y + profile.capsuleHeight - profile.capsuleRadius;
        auto moved = world.moveCapsuleOwned(current.x, lowerY, current.z, current.x, upperY, current.z,
                                            profile.capsuleRadius, desired.x, desired.y, desired.z, filter);
        ++queryCount;
        if (!moved) return eve::Result<void>::failure(moved.status());
        const Vec3 actual{moved.value().deltaX, moved.value().deltaY, moved.value().deltaZ};
        if (length(subtract(desired, actual)) > profile.skin + 0.001f)
            return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                                 "climbing.anchor.path_blocked", "candidate.path." + std::to_string(segment));
        current = target;
    }
    return eve::Result<void>::success();
}

eve::Result<ClimbingCandidate> makeCandidate(physics::World3D& world, ClimbingAnchorGraphInstance& graph,
                                             const ResolvedClimbingAnchorNode& node,
                                             const ClimbingActionDefinition& action,
                                             const ClimbingProfileDefinition& profile,
                                             const ClimbingPose& pose, std::uint64_t definitionGeneration) {
    physics::Body3D* body = world.findBody(graph.body());
    if (!body)
        return failure<ClimbingCandidate>(eve::DiagnosticCode::StaleHandle,
                                          "anchor graph target body is stale", "graph.body");
    ClimbingCandidate candidate;
    candidate.actionId = action.id;
    candidate.definitionGeneration = definitionGeneration;
    candidate.world = world.runtimeHandle();
    candidate.obstacleBody = graph.body();
    candidate.obstacleBodyId = body->getId();
    candidate.ignoredBodyId = pose.ignoredBodyId;
    candidate.frontPoint = node.position;
    candidate.topPoint = node.position;
    candidate.surfaceNormal = node.normal;
    candidate.surfaceTangent = node.tangent;
    candidate.leftHandAnchor = node.leftHandSocket;
    candidate.rightHandAnchor = node.rightHandSocket;
    candidate.landingFeet = action.kind == ClimbingActionKind::LadderDismount ||
                                    action.kind == ClimbingActionKind::BeamBalance
                                ? node.position
                                : hangingFeet(node, action, profile);
    candidate.obstacleHeight = length(subtract(candidate.landingFeet, pose.feet));
    candidate.kind = action.kind;
    candidate.support = action.kind == ClimbingActionKind::LadderDismount ||
                                action.kind == ClimbingActionKind::BeamBalance
                            ? HangSupport::None
                            : (node.kind == ClimbingAnchorKind::LadderRung ? HangSupport::Braced : HangSupport::Free);
    auto localTop = body->worldToLocalPointOwned(node.position.x, node.position.y, node.position.z);
    if (!localTop) return eve::Result<ClimbingCandidate>::failure(localTop.status());
    auto localLanding = body->worldToLocalPointOwned(candidate.landingFeet.x, candidate.landingFeet.y,
                                                     candidate.landingFeet.z);
    if (!localLanding) return eve::Result<ClimbingCandidate>::failure(localLanding.status());
    candidate.bodyLocalTop = {localTop.value().x, localTop.value().y, localTop.value().z};
    candidate.bodyLocalLanding = {localLanding.value().x, localLanding.value().y, localLanding.value().z};
    return eve::Result<ClimbingCandidate>::success(std::move(candidate));
}

}  // namespace

ClimbingRuntime::~ClimbingRuntime() noexcept {
    auto released = releaseAnchorReservation();
    released.ignore("climbing runtime destruction releases graph occupancy");
}

eve::Result<void> ClimbingRuntime::releaseAnchorReservation() {
    if (!execution_ || execution_->anchorReservation.id.isZero())
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    auto graph = Climbing::resolveAnchorGraph(execution_->anchorGraph);
    if (!graph.isBound()) {
        execution_->anchorReservation = {};
        execution_->anchorGraph = {};
        execution_->anchorNode = {};
        return failure<void>(eve::DiagnosticCode::StaleHandle,
                             "anchor graph instance is stale while releasing occupancy", "execution.anchorGraph");
    }
    auto released = graph->release(execution_->anchorReservation);
    if (released || (released.error() &&
                     (released.error()->code() == eve::DiagnosticCode::StaleHandle ||
                      released.error()->code() == eve::DiagnosticCode::NotFound ||
                      released.error()->code() == eve::DiagnosticCode::Conflict))) {
        execution_->anchorReservation = {};
        execution_->anchorGraph = {};
        execution_->anchorNode = {};
    }
    if (!released && released.error() && released.error()->code() == eve::DiagnosticCode::Conflict)
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    return released;
}

eve::Result<ClimbingAnchorNodeRef> ClimbingRuntime::currentAnchor() const {
    if (!execution_ || !execution_->anchorGraph.isValid() || execution_->anchorReservation.id.isZero())
        return failure<ClimbingAnchorNodeRef>(eve::DiagnosticCode::NotFound,
                                              "active execution is not bound to an anchor graph", "execution.anchor");
    return eve::Result<ClimbingAnchorNodeRef>::success(execution_->anchorNode);
}

eve::Result<ClimbingCandidate> ClimbingRuntime::tryBeginAnchor(
    physics::World3D& world, ClimbingAnchorGraphHandleRef graphReference, const ClimbingAnchorNodeRef& nodeReference,
    ClimbingAnchorAgentId agentId, std::string_view actionId, const ClimbingPose& pose, eve::SimulationTick tick) {
    if (activePhase(phase_) || execution_)
        return failure<ClimbingCandidate>(eve::DiagnosticCode::Conflict,
                                          "a climbing execution is already active", "runtime.phase");
    if (nextExecutionId_ == 0 || nextExecutionId_ == std::numeric_limits<std::uint64_t>::max())
        return failure<ClimbingCandidate>(eve::DiagnosticCode::PreconditionViolation,
                                          "climbing execution id space is exhausted", "runtime.nextExecutionId");
    if (agentId.isZero())
        return failure<ClimbingCandidate>(eve::DiagnosticCode::InvalidArgument,
                                          "anchor execution requires a stable non-zero agent id", "agentId");
    auto graph = Climbing::resolveAnchorGraph(graphReference);
    if (!graph.isBound())
        return failure<ClimbingCandidate>(eve::DiagnosticCode::StaleHandle,
                                          "anchor graph instance handle is stale", "graph");
    auto resolved = graph->resolveNode(world, nodeReference);
    if (!resolved) return eve::Result<ClimbingCandidate>::failure(resolved.status());
    const ClimbingActionDefinition* action = findAction(profile_, actionId);
    if (!action || !startKindMatches(resolved.value().kind, action->kind))
        return failure<ClimbingCandidate>(eve::DiagnosticCode::InvalidArgument,
                                          "action kind cannot approach the requested anchor node", "actionId");
    if (!action->requiredNotifies.empty() && !validatedAnimationActions_.contains(action->id))
        return failure<ClimbingCandidate>(eve::DiagnosticCode::PreconditionViolation,
                                          "climbing.animation.notify_missing: action clip contract was not validated",
                                          "actionId");
    auto candidate = makeCandidate(world, *graph, resolved.value(), *action, profile_, pose, definitionGeneration_);
    if (!candidate) return candidate;
    if (candidate.value().obstacleHeight + epsilon < action->minHeight ||
        candidate.value().obstacleHeight - epsilon > action->maxHeight)
        return failure<ClimbingCandidate>(eve::DiagnosticCode::PreconditionViolation,
                                          "anchor approach lies outside the action geometry range", "action.height");
    auto path = validatePath(world, pose.feet, candidate.value(), *action, profile_, lastQueryCount_);
    if (!path) return eve::Result<ClimbingCandidate>::failure(path.status());
    auto eventCapacity = requireEventCapacity(1, tick);
    if (!eventCapacity) return eve::Result<ClimbingCandidate>::failure(eventCapacity.status());
    const ClimbingExecutionId executionId(nextExecutionId_);
    auto reservation = graph->reserve(nodeReference, {agentId, executionId});
    if (!reservation) return eve::Result<ClimbingCandidate>::failure(reservation.status());

    PreparedBegin prepared;
    prepared.executionId = executionId;
    prepared.candidate = candidate.value();
    prepared.action = *action;
    prepared.startFeet = pose.feet;
    prepared.tick = tick;
    prepared.conditionsSatisfied = action->requiredConditionTags.empty();
    auto committed = commitBegin(std::move(prepared));
    if (!committed) {
        auto rollback = graph->release(reservation.value());
        rollback.ignore("rollback graph reservation after climbing begin commit failure");
        return eve::Result<ClimbingCandidate>::failure(committed.status());
    }
    execution_->anchorGraph = graphReference;
    execution_->anchorNode = nodeReference;
    execution_->anchorReservation = std::move(reservation).takeValue();
    return eve::Result<ClimbingCandidate>::success(std::move(committed).takeValue().candidate,
                                                    eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::transitionAnchor(physics::World3D& world,
                                                     const ClimbingAnchorNodeRef& target,
                                                     ClimbingAnchorEdgeKind edgeKind,
                                                     std::string_view actionId,
                                                     eve::SimulationTick tick) {
    if ((phase_ != ClimbingPhase::Hanging && phase_ != ClimbingPhase::Balanced &&
         phase_ != ClimbingPhase::Swinging) || !execution_ || !execution_->anchorGraph.isValid() ||
        execution_->anchorReservation.id.isZero())
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "anchor transition requires a graph-bound hanging execution", "runtime.phase");
    if (!execution_->branchWindowOpen)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "anchor transition branch window is closed", "execution.branchWindow");
    if (tick <= execution_->lastTick)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "anchor transition tick must be newer than the last execution tick", "tick");
    auto graph = Climbing::resolveAnchorGraph(execution_->anchorGraph);
    if (!graph.isBound())
        return failure<void>(eve::DiagnosticCode::StaleHandle,
                             "anchor graph instance handle is stale", "execution.anchorGraph");
    auto reservationValid = graph->validateReservation(execution_->anchorReservation);
    if (!reservationValid) return reservationValid;
    auto edges = graph->edgesFrom(execution_->anchorNode);
    if (!edges) return eve::Result<void>::failure(edges.status());
    const auto edge = std::find_if(edges.value().begin(), edges.value().end(), [&](const auto& candidate) {
        return candidate.to == target.nodeId && candidate.kind == edgeKind;
    });
    if (edge == edges.value().end())
        return failure<void>(eve::DiagnosticCode::NotFound,
                             "no authored edge connects the current and requested anchors", "target");
    auto resolved = graph->resolveNode(world, target);
    if (!resolved) return eve::Result<void>::failure(resolved.status());
    if (!tagsContain(resolved.value().tags, edge->requiredTags))
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "target anchor does not satisfy the edge tag contract", "target.tags");
    const ClimbingActionDefinition* action = findAction(profile_, actionId);
    if (!action || !edgeKindMatches(edgeKind, action->kind))
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "action kind does not match the authored anchor edge", "actionId");
    if (!action->requiredNotifies.empty() && !validatedAnimationActions_.contains(action->id))
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "climbing.animation.notify_missing: action clip contract was not validated", "actionId");
    ClimbingPose pose;
    pose.feet = execution_->currentFeet;
    pose.forward = scale(execution_->candidate.surfaceNormal, -1.f);
    pose.ignoredBodyId = execution_->candidate.ignoredBodyId;
    pose.grounded = false;
    auto candidate = makeCandidate(world, *graph, resolved.value(), *action, profile_, pose, definitionGeneration_);
    if (!candidate) return eve::Result<void>::failure(candidate.status());
    const float span = length(subtract(candidate.value().landingFeet, execution_->currentFeet));
    candidate.value().obstacleHeight = span;
    if (span + epsilon < action->minHeight || span - epsilon > action->maxHeight)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "anchor transition lies outside the action geometry range", "action.height");
    auto path = validatePath(world, execution_->currentFeet, candidate.value(), *action, profile_, lastQueryCount_);
    if (!path) return eve::Result<void>::failure(path.status());
    auto eventCapacity = requireEventCapacity(1, tick);
    if (!eventCapacity) return eventCapacity;
    auto nextReservation = graph->reserve(target, execution_->anchorReservation.occupant);
    if (!nextReservation) return eve::Result<void>::failure(nextReservation.status());
    auto released = graph->release(execution_->anchorReservation);
    if (!released) {
        auto rollback = graph->release(nextReservation.value());
        rollback.ignore("rollback target reservation after source release failure");
        return released;
    }

    execution_->candidate = std::move(candidate).takeValue();
    execution_->action = *action;
    execution_->definitionGeneration = definitionGeneration_;
    execution_->startFeet = execution_->currentFeet;
    execution_->lastPlannedFeet = execution_->currentFeet;
    execution_->elapsed = eve::Duration::zero();
    execution_->duration = action->duration;
    execution_->lastTick = tick;
    execution_->accumulatedResidual = {};
    execution_->horizontalWarpUsed = 0.f;
    execution_->verticalWarpUsed = 0.f;
    execution_->facingWarpUsed = 0.f;
    execution_->leftContactEmitted = false;
    execution_->rightContactEmitted = false;
    execution_->landContactReleased = false;
    execution_->compactCollisionActive = false;
    execution_->branchWindowOpen = false;
    execution_->anchorNode = target;
    execution_->anchorReservation = std::move(nextReservation).takeValue();
    phase_ = ClimbingPhase::Launching;
    terminalCode_.clear();
    enqueueEvent({ClimbingEventKind::AnchorTransitionStarted, execution_->candidate.actionId, tick,
                  execution_->executionId});
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::climbing
