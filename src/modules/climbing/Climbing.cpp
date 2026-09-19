#include "climbing/Climbing.h"

#include "climbing/ClimbingCodec.h"
#include "climbing/ClimbingRuntimeInternal.h"
#include "climbing/ClimbingTrajectory.h"

#include "animation/AnimClip.h"
#include "common/Assert.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::climbing {
using namespace runtime_detail;


void ClimbingCandidateSet::consider(ClimbingCandidate candidate) {
    std::string actionId;
    actionId.swap(candidate.actionId);
    considerWithActionId(std::move(candidate), actionId);
}

void ClimbingCandidateSet::considerWithActionId(ClimbingCandidate candidate, std::string_view actionId) {
    const auto store = [&](ClimbingCandidate& slot) {
        std::string reused = std::move(slot.actionId);
        slot = std::move(candidate);
        if (reused.capacity() >= actionId.size()) {
            reused.assign(actionId);
            slot.actionId.swap(reused);
        } else {
            slot.actionId.assign(actionId);
        }
    };
    if (size_ < Capacity) {
        store(values_[size_++]);
        return;
    }
    auto       worst  = std::max_element(values_.begin(), values_.end(), isCandidateLess);
    const bool better = candidate.score != worst->score
                            ? candidate.score < worst->score
                            : actionId != worst->actionId
                                  ? actionId < std::string_view(worst->actionId)
                                  : candidate.obstacleBodyId != worst->obstacleBodyId
                                        ? candidate.obstacleBodyId < worst->obstacleBodyId
                                        : candidate.obstacleShapeId < worst->obstacleShapeId;
    if (better) store(*worst);
}

void ClimbingCandidateSet::sortAndLimit(std::size_t limit) {
    std::sort(values_.begin(), values_.begin() + static_cast<std::ptrdiff_t>(size_), isCandidateLess);
    size_ = std::min(size_, std::min(limit, Capacity));
}

void ClimbingCandidateSet::swap(ClimbingCandidateSet& other) noexcept {
    values_.swap(other.values_);
    std::swap(size_, other.size_);
}

eve::Result<void> ClimbingRuntime::requireEventCapacity(std::size_t count, eve::SimulationTick tick) const {
    if (count > PendingEventCapacity - pendingEvents_.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "climbing event queue must be drained before simulation can continue", "runtime.pendingEvents", {}, "climbing"));
    if (!pendingEvents_.empty() && tick < pendingEvents_.back().tick)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "climbing event tick must not precede an undelivered event", "runtime.pendingEvents.tick", {}, "climbing"));
    return eve::Result<void>::success();
}

void ClimbingRuntime::enqueueEvent(ClimbingEvent event) {
    EV_ASSERT(pendingEvents_.size() < PendingEventCapacity, "climbing event queue capacity invariant");
    const bool ordered = pendingEvents_.empty() || pendingEvents_.back().tick <= event.tick;
    EV_ASSERT(ordered, "climbing event production order must be monotonic by tick");
    if (event.metadata.empty() && execution_ && execution_->action.id == event.actionId)
        event.metadata = execution_->action.eventMetadata;
    pendingEvents_.push_back(std::move(event));
}

eve::Result<std::vector<ClimbingEvent>> ClimbingRuntime::drainEvents() {
    std::vector<ClimbingEvent> events;
    events.swap(pendingEvents_);
    const eve::StatusCode code = events.empty() ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<std::vector<ClimbingEvent>>::success(std::move(events), eve::Status::success(code));
}

eve::Result<void> ClimbingRuntime::setProfile(ClimbingProfile profile) {
    return publishProfile(std::move(profile), false);
}

ClimbingLocomotionPolicy ClimbingRuntime::locomotionPolicy() const noexcept {
    return {profile_.groundAcceleration, profile_.groundBraking, profile_.airControl,
            profile_.gravity, profile_.jumpSpeed, profile_.coyoteTicks, profile_.queryFilter};
}

eve::Result<void> ClimbingRuntime::publishProfile(ClimbingProfile profile, bool allowActiveExecution) {
    auto valid = validateClimbingProfileDefinition(profile);
    if (!valid) return eve::Result<void>::failure(valid.status());
    if (!allowActiveExecution && isActivePhase(phase_))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "profile cannot change while an execution is active", "runtime.phase", {}, "climbing"));
    if (definitionGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing definition generation is exhausted", "runtime.definitionGeneration", {}, "climbing"));
    std::sort(profile.actions.begin(), profile.actions.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    const auto duplicate = std::adjacent_find(profile.actions.begin(), profile.actions.end(),
                                              [](const auto& lhs, const auto& rhs) { return lhs.id == rhs.id; });
    if (duplicate != profile.actions.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "profile contains duplicate action ids", "profile.actions", {}, "climbing"));
    profile_ = std::move(profile);
    validatedAnimationActions_.clear();
    ++definitionGeneration_;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::upsertAction(ClimbingActionDefinition action) {
    auto valid = validateClimbingActionDefinition(action);
    if (!valid) return eve::Result<void>::failure(valid.status());
    if (definitionGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing definition generation is exhausted", "runtime.definitionGeneration", {}, "climbing"));
    const std::string actionId = action.id;
    auto              found = std::lower_bound(profile_.actions.begin(), profile_.actions.end(), action.id,
                                               [](const auto& entry, const std::string& id) { return entry.id < id; });
    if (found != profile_.actions.end() && found->id == action.id)
        *found = std::move(action);
    else
        profile_.actions.insert(found, std::move(action));
    validatedAnimationActions_.erase(actionId);
    ++definitionGeneration_;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::validateAnimationBinding(std::string_view           actionId,
                                                            const animation::AnimClip& clip) {
    const ClimbingActionDefinition* action = findAction(profile_, actionId);
    if (!action)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "animation binding references an unknown action", "actionId", {}, "climbing"));
    if (!action->animation.clipId.empty() && clip.getName() != action->animation.clipId)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "animation clip does not match the action binding", "action.animation.clipId", {}, "climbing"));
    auto validated = clip.validateNotifyContract(action->requiredNotifies);
    if (!validated)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climbing.animation.notify_missing: " + validated.status().describe(), "action.requiredNotifies", {}, "climbing"));
    validatedAnimationActions_.insert(action->id);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingCandidateSet> ClimbingRuntime::probe(physics::World3D& world,
                                                         const ClimbingPose& pose) const {
    ClimbingCandidateSet output;
    auto probed = probeInto(world, pose, output);
    if (!probed) return eve::Result<ClimbingCandidateSet>::failure(probed.status());
    return eve::Result<ClimbingCandidateSet>::success(std::move(output));
}

eve::Result<void> ClimbingRuntime::probeInto(physics::World3D& world, const ClimbingPose& pose,
                                              ClimbingCandidateSet& output,
                                              eve::SimulationTick tick) const {
    lastCandidates_.clear();
    lastQueryCount_ = 0;
    lastCounters_ = {};
    lastCounters_.workload = ClimbingWorkload::CandidateProbe;
    lastCounters_.queryBudget = ClimbingQueryBudgets::CandidateProbe;
    RuntimeTelemetryScope telemetryScope(telemetry_, lastCounters_, tick);
    if (debugCapture_ == ClimbingDebugCapture::Enabled) {
        lastDebugQueries_.clear();
        lastEvidence_.clear();
    }
    auto valid      = validateClimbingProfileDefinition(profile_);
    if (!valid) return eve::Result<void>::failure(valid.status());
    if (!world.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid", "world", {}, "climbing"));
    if (!isFinite(pose.feet) || !isFinite(pose.forward) || !isFinite(pose.speed) || pose.speed < 0.f ||
        !isFinite(pose.verticalSpeed) || !isFinite(pose.moveIntent) || !isFinite(pose.lookIntent))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "pose values must be finite and speed non-negative", "pose", {}, "climbing"));
    const Vec3 forward = normalizedHorizontal(pose.forward);
    if (lengthSquared(forward) <= epsilon)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "pose forward must have a horizontal direction", "pose.forward", {}, "climbing"));

    ClimbingCandidateSet& candidates = probeScratch_;
    candidates.clear();
    const auto reserveQuery = [&]() noexcept {
        if (lastQueryCount_ >= lastCounters_.queryBudget) {
            lastCounters_.budgetState = ClimbingQueryBudgetState::Exceeded;
            return false;
        }
        ++lastQueryCount_;
        lastCounters_.queryCount = lastQueryCount_;
        return true;
    };
    const auto rejectCandidate = [&]() noexcept { ++lastCounters_.rejectCount; };
    const auto finalizeCandidates = [&]() -> eve::Result<void> {
        candidates.sortAndLimit(profile_.maxCandidates);
        if (!candidates.empty()) lastCounters_.selectedCost = candidates.front().score;
        if (debugCapture_ == ClimbingDebugCapture::Enabled)
            lastCandidates_.assign(candidates.begin(), candidates.end());
        candidates.swap(output);
        return eve::Result<void>::success();
    };

    const auto captureBodyLocal = [&](ClimbingCandidate& candidate) -> eve::Result<void> {
        if (physics::Body3D* body = world.findBody(candidate.obstacleBody)) {
            auto localTop = body->worldToLocalPointOwned(candidate.topPoint.x, candidate.topPoint.y,
                                                         candidate.topPoint.z);
            if (!localTop) return eve::Result<void>::failure(localTop.status());
            auto localLanding = body->worldToLocalPointOwned(candidate.landingFeet.x, candidate.landingFeet.y,
                                                             candidate.landingFeet.z);
            if (!localLanding) return eve::Result<void>::failure(localLanding.status());
            candidate.bodyLocalTop = {localTop.value().x, localTop.value().y, localTop.value().z};
            candidate.bodyLocalLanding = {localLanding.value().x, localLanding.value().y,
                                          localLanding.value().z};
        }
        return eve::Result<void>::success();
    };

    physics::QueryFilter3D filter = profile_.queryFilter;
    filter.ignoredBodyId          = pose.ignoredBodyId;
    const float broadRadius = profile_.maxProbeDistance + profile_.capsuleRadius + profile_.skin;
    auto broadPhase = world.queryAabbBroadPhaseOwned(
        pose.feet.x - broadRadius, pose.feet.y - profile_.skin, pose.feet.z - broadRadius,
        pose.feet.x + broadRadius, pose.feet.y + profile_.maxObstacleHeight + profile_.capsuleHeight,
        pose.feet.z + broadRadius, filter);
    if (!broadPhase) return eve::Result<void>::failure(broadPhase.status());
    lastCounters_.broadPhaseQueryCount = 1;
    lastCounters_.broadPhaseHitCount = static_cast<std::uint32_t>(broadPhase.value().count);
    if (broadPhase.value().count == 0) return finalizeCandidates();
    const auto broadPhaseContains = [&](physics::PhysicsShapeHandle shape) noexcept {
        if (broadPhase.value().truncated) return true;
        return std::any_of(broadPhase.value().hits.begin(),
                           broadPhase.value().hits.begin() +
                               static_cast<std::ptrdiff_t>(broadPhase.value().count),
                           [&](const physics::BroadPhaseHit3D& hit) { return hit.shape == shape; });
    };

    const bool hasSlide = std::any_of(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
        return action.kind == ClimbingActionKind::Slide && isActionEnabledForPose(profile_, action, pose) &&
               isProbeRecipeMatchingKind(action);
    });
    if (hasSlide && pose.grounded) {
        const Vec3 groundStart{pose.feet.x, pose.feet.y + 0.25f, pose.feet.z};
        const Vec3 groundEnd{pose.feet.x, pose.feet.y - 0.35f, pose.feet.z};
        if (!reserveQuery()) return finalizeCandidates();
        auto groundResult = world.rayCastOwned(groundStart.x, groundStart.y, groundStart.z,
                                               groundEnd.x, groundEnd.y, groundEnd.z, filter);
        if (!groundResult) return eve::Result<void>::failure(groundResult.status());
        const physics::RayHit3D ground = std::move(groundResult).takeValue();
        if (debugCapture_ == ClimbingDebugCapture::Enabled)
            boundedDebugPush(lastDebugQueries_, ClimbingDebugQuery{"", ground.hit ? "slide.ground" : "slide.no_ground",
                                                                    groundStart, groundEnd});
        if (ground.hit && broadPhaseContains(ground.shape) && ground.normalY >= profile_.minTopNormalY) {
            for (const auto& action : profile_.actions) {
                if (action.kind != ClimbingActionKind::Slide || !isActionEnabledForPose(profile_, action, pose) ||
                    !isProbeRecipeMatchingKind(action))
                    continue;
                if (pose.speed + epsilon < action.minSpeed) {
                    rejectCandidate();
                    if (debugCapture_ == ClimbingDebugCapture::Enabled)
                        boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "below_min_speed"});
                    continue;
                }
                if (!isSupportSelectorMatch(action, ground.shapeTag, ground.materialId)) {
                    rejectCandidate();
                    if (debugCapture_ == ClimbingDebugCapture::Enabled)
                        boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "support_tag_rejected"});
                    continue;
                }
                const Vec3 landing = pose.feet + forward * action.landingForward;
                const float lowerY = landing.y + profile_.capsuleRadius + profile_.skin;
                const float upperY = landing.y + profile_.compactCapsuleHeight - profile_.capsuleRadius + profile_.skin;
                if (!reserveQuery()) return finalizeCandidates();
                auto overlap = world.queryCapsuleOwned(landing.x, lowerY, landing.z, landing.x, upperY, landing.z,
                                                       profile_.capsuleRadius, filter);
                if (!overlap) return eve::Result<void>::failure(overlap.status());
                if (overlap.value().bodyCount != 0) {
                    rejectCandidate();
                    if (debugCapture_ == ClimbingDebugCapture::Enabled)
                        boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "clearance_blocked"});
                    continue;
                }
                ClimbingCandidate candidate;
                candidate.definitionGeneration = definitionGeneration_;
                candidate.world = world.runtimeHandle();
                candidate.obstacleBody = ground.body;
                candidate.obstacleShape = ground.shape;
                candidate.obstacleBodyId = ground.bodyId;
                candidate.obstacleShapeId = ground.shapeId;
                candidate.ignoredBodyId = pose.ignoredBodyId;
                candidate.frontPoint = {ground.x, ground.y, ground.z};
                candidate.topPoint = candidate.frontPoint;
                candidate.landingFeet = landing;
                candidate.surfaceNormal = {ground.normalX, ground.normalY, ground.normalZ};
                candidate.surfaceTangent = forward;
                candidate.gapDistance = action.landingForward;
                candidate.clearanceHeight = profile_.compactCapsuleHeight;
                candidate.slopeRadians = std::acos(std::clamp(ground.normalY, -1.f, 1.f));
                candidate.supportShapeTag = ground.shapeTag;
                candidate.supportMaterialId = ground.materialId;
                candidate.probeRecipe = action.probeRecipe;
                candidate.score = selectionCost(profile_, action, pose, forward, landing - pose.feet,
                                                0.f, action.landingForward) +
                                  (previousActionId_ == action.id ? action.repetitionPenalty : 0);
                candidate.kind = action.kind;
                auto captured = captureBodyLocal(candidate);
                if (!captured) return eve::Result<void>::failure(captured.status());
                if (debugCapture_ == ClimbingDebugCapture::Enabled)
                    boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{
                                                        action.id, "accepted", static_cast<std::int64_t>(action.selectionBias) * 1000000ll,
                                                        0, quantizeMillimeters(action.landingForward), candidate.score});
                candidates.considerWithActionId(std::move(candidate), action.id);
            }
        }
    }

    const bool hasWallRun = std::any_of(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
        return action.kind == ClimbingActionKind::WallRun && isActionEnabledForPose(profile_, action, pose) &&
               isProbeRecipeMatchingKind(action);
    });
    if (hasWallRun) {
        const Vec3 chest{pose.feet.x, pose.feet.y + profile_.capsuleHeight * 0.55f, pose.feet.z};
        const Vec3 sides[2] = {{-forward.z, 0.f, forward.x}, {forward.z, 0.f, -forward.x}};
        for (std::size_t sideIndex = 0; sideIndex < 2; ++sideIndex) {
            const Vec3 end = chest + sides[sideIndex] * profile_.maxProbeDistance;
            if (!reserveQuery()) return finalizeCandidates();
            auto sideResult = world.rayCastOwned(chest.x, chest.y, chest.z, end.x, end.y, end.z, filter);
            if (!sideResult) return eve::Result<void>::failure(sideResult.status());
            const physics::RayHit3D hit = std::move(sideResult).takeValue();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastDebugQueries_, ClimbingDebugQuery{"", hit.hit ? "wall_run.wall" : "wall_run.miss",
                                                                        chest, end});
            Vec3 normal = hit.hit ? normalizedHorizontal({hit.normalX, hit.normalY, hit.normalZ}) : Vec3{};
            if (!hit.hit || !broadPhaseContains(hit.shape) || lengthSquared(normal) <= epsilon ||
                std::fabs(hit.normalY) > 0.35f)
                continue;
            Vec3 tangent{normal.z, 0.f, -normal.x};
            if (tangent.x * forward.x + tangent.z * forward.z < 0.f) tangent = tangent * -1.f;
            for (const auto& action : profile_.actions) {
                if (action.kind != ClimbingActionKind::WallRun || !isActionEnabledForPose(profile_, action, pose) ||
                    !isProbeRecipeMatchingKind(action) || pose.speed + epsilon < action.minSpeed)
                    continue;
                if (!isSupportSelectorMatch(action, hit.shapeTag, hit.materialId)) {
                    rejectCandidate();
                    continue;
                }
                const float wallOffset = profile_.capsuleRadius + profile_.skin;
                const Vec3 landing{hit.x + normal.x * wallOffset + tangent.x * action.landingForward,
                                   pose.feet.y, hit.z + normal.z * wallOffset + tangent.z * action.landingForward};
                const float lowerY = landing.y + profile_.capsuleRadius + profile_.skin;
                const float upperY = landing.y + profile_.capsuleHeight - profile_.capsuleRadius + profile_.skin;
                if (!reserveQuery()) return finalizeCandidates();
                auto overlap = world.queryCapsuleOwned(landing.x, lowerY, landing.z, landing.x, upperY, landing.z,
                                                       profile_.capsuleRadius, filter);
                if (!overlap) return eve::Result<void>::failure(overlap.status());
                if (overlap.value().bodyCount != 0) {
                    rejectCandidate();
                    continue;
                }
                ClimbingCandidate candidate;
                candidate.definitionGeneration = definitionGeneration_;
                candidate.world = world.runtimeHandle();
                candidate.obstacleBody = hit.body;
                candidate.obstacleShape = hit.shape;
                candidate.obstacleBodyId = hit.bodyId;
                candidate.obstacleShapeId = hit.shapeId;
                candidate.ignoredBodyId = pose.ignoredBodyId;
                candidate.frontPoint = {hit.x, hit.y, hit.z};
                candidate.topPoint = candidate.frontPoint;
                candidate.landingFeet = landing;
                candidate.surfaceNormal = normal;
                candidate.surfaceTangent = tangent;
                const float distance = std::sqrt((hit.x - chest.x) * (hit.x - chest.x) +
                                                  (hit.z - chest.z) * (hit.z - chest.z));
                candidate.gapDistance = distance;
                candidate.clearanceHeight = profile_.capsuleHeight;
                candidate.slopeRadians = std::acos(std::clamp(hit.normalY, -1.f, 1.f));
                candidate.supportShapeTag = hit.shapeTag;
                candidate.supportMaterialId = hit.materialId;
                candidate.probeRecipe = action.probeRecipe;
                candidate.score = selectionCost(profile_, action, pose, tangent, landing - pose.feet,
                                                0.f, distance, static_cast<std::int64_t>(sideIndex)) +
                                  (previousActionId_ == action.id ? action.repetitionPenalty : 0) +
                                  0;
                candidate.kind = action.kind;
                auto captured = captureBodyLocal(candidate);
                if (!captured) return eve::Result<void>::failure(captured.status());
                if (debugCapture_ == ClimbingDebugCapture::Enabled)
                    boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{
                                                        action.id, "accepted", static_cast<std::int64_t>(action.selectionBias) * 1000000ll,
                                                        0, quantizeMillimeters(distance), candidate.score});
                candidates.considerWithActionId(std::move(candidate), action.id);
            }
        }
    }

    const bool hasObstacleProbe =
        std::any_of(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
            return isObstacleProbeKind(action.kind) && isActionEnabledForPose(profile_, action, pose) &&
                   isProbeRecipeMatchingKind(action);
        });
    if (!hasObstacleProbe) return finalizeCandidates();
    // A single chest-height ray skips low vault obstacles entirely. The obstacle recipe starts just above the
    // capsule's lower hemisphere so it can see every valid obstacle above the profile's physical skin; top and
    // landing queries still provide the authoritative height and clearance rejection.
    const float obstacleProbeY = pose.feet.y + profile_.capsuleRadius + profile_.skin;
    const Vec3  frontStart{pose.feet.x, obstacleProbeY, pose.feet.z};
    const Vec3  frontEnd = frontStart + forward * profile_.maxProbeDistance;
    if (!reserveQuery()) return finalizeCandidates();
    auto        frontResult =
        world.rayCastOwned(frontStart.x, frontStart.y, frontStart.z, frontEnd.x, frontEnd.y, frontEnd.z, filter);
    if (!frontResult) return eve::Result<void>::failure(frontResult.status());
    const physics::RayHit3D front = std::move(frontResult).takeValue();
    if (debugCapture_ == ClimbingDebugCapture::Enabled)
        boundedDebugPush(lastDebugQueries_, ClimbingDebugQuery{"", front.hit ? "front.hit" : "front.miss",
                                                                frontStart, frontEnd});
    if (!front.hit || !broadPhaseContains(front.shape)) return finalizeCandidates();

    const Vec3 overFront{front.x + forward.x * (profile_.capsuleRadius + profile_.skin),
                         pose.feet.y + profile_.maxObstacleHeight + profile_.skin,
                         front.z + forward.z * (profile_.capsuleRadius + profile_.skin)};
    const Vec3 topEnd{overFront.x, pose.feet.y + profile_.skin, overFront.z};
    if (!reserveQuery()) return finalizeCandidates();
    auto topResult = world.rayCastOwned(overFront.x, overFront.y, overFront.z, topEnd.x, topEnd.y, topEnd.z, filter);
    if (!topResult) return eve::Result<void>::failure(topResult.status());
    const physics::RayHit3D top = std::move(topResult).takeValue();
    if (debugCapture_ == ClimbingDebugCapture::Enabled)
        boundedDebugPush(lastDebugQueries_, ClimbingDebugQuery{"", top.hit ? "top.hit" : "top.miss", overFront,
                                                                topEnd});
    if (!top.hit || top.body != front.body || top.normalY < profile_.minTopNormalY)
        return finalizeCandidates();

    const float height = top.y - pose.feet.y;
    if (height < 0.f || height > profile_.maxObstacleHeight + profile_.skin)
        return finalizeCandidates();

    const float baseDepth = std::sqrt((top.x - front.x) * (top.x - front.x) +
                                      (top.z - front.z) * (top.z - front.z));
    float measuredDepth = baseDepth;
    float measuredCurvature = 0.f;
    const float topSlope = std::acos(std::clamp(top.normalY, -1.f, 1.f));
    float depthProbeDistance = 0.f;
    bool needsDepthEvidence = false;
    for (const ClimbingActionDefinition& action : profile_.actions) {
        if (!isObstacleProbeKind(action.kind) || !isActionEnabledForPose(profile_, action, pose) ||
            !isProbeRecipeMatchingKind(action))
            continue;
        if (action.minDepth > baseDepth + epsilon || action.maxDepth < 999.f || action.maxCurvature < 999.f) {
            needsDepthEvidence = true;
            depthProbeDistance = std::max(depthProbeDistance, std::max(action.minDepth, action.maxDepth < 999.f
                                                                                           ? action.maxDepth + profile_.skin
                                                                                           : 0.f));
        }
    }
    depthProbeDistance = std::min(profile_.maxProbeDistance, std::max(0.f, depthProbeDistance - baseDepth));
    if (needsDepthEvidence && depthProbeDistance > epsilon) {
        const std::uint32_t sampleCount = std::min<std::uint32_t>(4, profile_.probeSectors);
        Vec3 previousNormal{top.normalX, top.normalY, top.normalZ};
        const float stepDistance = depthProbeDistance / static_cast<float>(sampleCount);
        for (std::uint32_t sample = 1; sample <= sampleCount; ++sample) {
            if (!reserveQuery()) break;
            const float offset = stepDistance * static_cast<float>(sample);
            const Vec3 sampleStart{top.x + forward.x * offset, top.y + profile_.skin + 0.2f,
                                   top.z + forward.z * offset};
            const Vec3 sampleEnd{sampleStart.x, top.y - profile_.skin - 0.2f, sampleStart.z};
            auto sampled = world.rayCastOwned(sampleStart.x, sampleStart.y, sampleStart.z, sampleEnd.x,
                                               sampleEnd.y, sampleEnd.z, filter);
            if (!sampled) return eve::Result<void>::failure(sampled.status());
            const physics::RayHit3D hit = std::move(sampled).takeValue();
            if (!hit.hit || hit.body != top.body || hit.normalY < profile_.minTopNormalY) break;
            measuredDepth = baseDepth + offset;
            const Vec3 normal{hit.normalX, hit.normalY, hit.normalZ};
            const float dot = std::clamp(previousNormal.x * normal.x + previousNormal.y * normal.y +
                                             previousNormal.z * normal.z,
                                         -1.f, 1.f);
            measuredCurvature = std::max(measuredCurvature, std::acos(dot) / std::max(stepDistance, epsilon));
            previousNormal = normal;
        }
    }

    for (const ClimbingActionDefinition& action : profile_.actions) {
        if (!isActionEnabledForPose(profile_, action, pose)) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "definition_policy_rejected"});
            continue;
        }
        if (!isProbeRecipeMatchingKind(action)) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "probe_recipe_rejected"});
            continue;
        }
        if (!isObstacleProbeKind(action.kind)) {
            if (isRuntimeProbeKind(action.kind)) continue;
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "anchor_graph_only"});
            continue;
        }
        if (height + epsilon < action.minHeight || height - epsilon > action.maxHeight) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "out_of_height_range"});
            continue;
        }
        if (measuredDepth + epsilon < action.minDepth || measuredDepth - epsilon > action.maxDepth ||
            topSlope - epsilon > action.maxSlopeRadians || measuredCurvature - epsilon > action.maxCurvature ||
            !isSupportSelectorMatch(action, top.shapeTag, top.materialId)) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "support_geometry_rejected"});
            continue;
        }
        if (pose.speed + epsilon < action.minSpeed) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "below_min_speed"});
            continue;
        }
        const bool wantsHang = action.kind == ClimbingActionKind::LedgeGrab;
        if ((wantsHang && (pose.grounded || pose.verticalSpeed > epsilon)) || (!wantsHang && !pose.grounded)) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "source_mode_rejected"});
            continue;
        }

        const Vec3 landing =
            wantsHang
                ? Vec3{front.x - forward.x * (profile_.capsuleRadius + profile_.skin + action.hangBodyOffset),
                       top.y - action.hangFeetBelowLedge,
                       front.z - forward.z * (profile_.capsuleRadius + profile_.skin + action.hangBodyOffset)}
                : Vec3{top.x + forward.x * action.landingForward, top.y, top.z + forward.z * action.landingForward};
        const float lowerY  = landing.y + profile_.capsuleRadius + profile_.skin;
        const float upperY  = landing.y + profile_.capsuleHeight - profile_.capsuleRadius + profile_.skin;
        if (!reserveQuery()) return finalizeCandidates();
        auto        overlap = world.queryCapsuleOwned(landing.x, lowerY, landing.z, landing.x, upperY, landing.z,
                                                      profile_.capsuleRadius, filter);
        if (!overlap) return eve::Result<void>::failure(overlap.status());
        if (debugCapture_ == ClimbingDebugCapture::Enabled)
            boundedDebugPush(lastDebugQueries_,
                             ClimbingDebugQuery{action.id,
                                                overlap.value().bodyCount == 0 ? "landing.clear" : "landing.blocked",
                                                landing, landing, profile_.capsuleRadius, profile_.capsuleHeight});
        if (overlap.value().bodyCount != 0) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "clearance_blocked"});
            continue;
        }

        HangSupport support = HangSupport::None;
        if (wantsHang) {
            const Vec3 supportStart{landing.x, landing.y + profile_.capsuleHeight * 0.45f, landing.z};
            const Vec3 supportEnd =
                supportStart + forward * (profile_.capsuleRadius + profile_.skin + action.hangBodyOffset + 0.25f);
            if (!reserveQuery()) return finalizeCandidates();
            auto supportHit = world.rayCastOwned(supportStart.x, supportStart.y, supportStart.z, supportEnd.x,
                                                 supportEnd.y, supportEnd.z, filter);
            if (!supportHit) return eve::Result<void>::failure(supportHit.status());
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastDebugQueries_,
                                 ClimbingDebugQuery{action.id,
                                                    supportHit.value().hit ? "support.hit" : "support.miss",
                                                    supportStart, supportEnd});
            support = supportHit.value().hit && supportHit.value().body == front.body ? HangSupport::Braced
                                                                                      : HangSupport::Free;
        }

        const float        distance = std::sqrt((front.x - pose.feet.x) * (front.x - pose.feet.x) +
                                                (front.z - pose.feet.z) * (front.z - pose.feet.z));
        if (distance + epsilon < action.minDistance || distance - epsilon > action.maxDistance ||
            front.normalY + epsilon < action.minSurfaceNormalY) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "geometry_policy_rejected"});
            continue;
        }
        const Vec3 approachNormal = normalizedHorizontal({-front.normalX, 0.f, -front.normalZ});
        const float heightCenter = (action.minHeight + action.maxHeight) * 0.5f;
        const std::int64_t score = selectionCost(profile_, action, pose, approachNormal,
                                                  landing - pose.feet, std::fabs(height - heightCenter),
                                                  distance) +
                                   (previousActionId_ == action.id ? action.repetitionPenalty : 0);
        if (debugCapture_ == ClimbingDebugCapture::Enabled) {
            const std::int64_t biasCost     = static_cast<std::int64_t>(action.selectionBias) * 1000000ll;
            const std::int64_t heightCost   = quantizeMillimeters(height) * 1000ll;
            const std::int64_t distanceCost = quantizeMillimeters(distance);
            boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "accepted", biasCost, heightCost,
                                                                       distanceCost, score});
        }
        ClimbingCandidate  candidate;
        candidate.definitionGeneration = definitionGeneration_;
        candidate.world           = world.runtimeHandle();
        candidate.obstacleBody    = front.body;
        candidate.obstacleShape   = front.shape;
        candidate.obstacleBodyId  = front.bodyId;
        candidate.obstacleShapeId = front.shapeId;
        candidate.ignoredBodyId   = pose.ignoredBodyId;
        candidate.frontPoint      = {front.x, front.y, front.z};
        candidate.topPoint        = {top.x, top.y, top.z};
        candidate.landingFeet     = landing;
        candidate.surfaceNormal   = {front.normalX, front.normalY, front.normalZ};
        candidate.surfaceTangent  = normalizedHorizontal({front.normalZ, 0.f, -front.normalX});
        candidate.leftHandAnchor  = candidate.topPoint - candidate.surfaceTangent * (action.handSpacing * 0.5f);
        candidate.rightHandAnchor = candidate.topPoint + candidate.surfaceTangent * (action.handSpacing * 0.5f);
        candidate.obstacleHeight  = height;
        candidate.obstacleDepth   = measuredDepth;
        candidate.gapDistance     = distance;
        candidate.clearanceHeight = profile_.capsuleHeight;
        candidate.slopeRadians    = topSlope;
        candidate.curvature       = measuredCurvature;
        candidate.supportShapeTag = top.shapeTag;
        candidate.supportMaterialId = top.materialId;
        candidate.probeRecipe     = action.probeRecipe;
        candidate.score           = score;
        candidate.kind            = action.kind;
        candidate.support         = support;
        if (physics::Body3D* body = world.findBody(front.body)) {
            auto localTop = body->worldToLocalPointOwned(top.x, top.y, top.z);
            if (!localTop) return eve::Result<void>::failure(localTop.status());
            auto localLanding = body->worldToLocalPointOwned(landing.x, landing.y, landing.z);
            if (!localLanding) return eve::Result<void>::failure(localLanding.status());
            candidate.bodyLocalTop = {localTop.value().x, localTop.value().y, localTop.value().z};
            candidate.bodyLocalLanding = {localLanding.value().x, localLanding.value().y,
                                          localLanding.value().z};
        }
        candidates.considerWithActionId(std::move(candidate), action.id);
    }
    return finalizeCandidates();
}

eve::Result<void> ClimbingRuntime::drop(eve::SimulationTick tick) {
    if (phase_ != ClimbingPhase::Hanging || !execution_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "drop requires a hanging execution", "runtime.phase", {}, "climbing"));
    if (tick <= execution_->lastTick)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "drop tick must be newer than the last execution tick", "tick", {}, "climbing"));
    auto eventCapacity = requireEventCapacity(1, tick);
    if (!eventCapacity) return eventCapacity;
    if (execution_->anchorGraph.isValid()) {
        auto released = releaseAnchorReservation();
        if (!released && released.error() && released.error()->code() != eve::DiagnosticCode::StaleHandle &&
            released.error()->code() != eve::DiagnosticCode::NotFound)
            return released;
    }
    enqueueEvent({ClimbingEventKind::Dropped, execution_->candidate.actionId, tick, execution_->executionId});
    execution_->velocity          = {0.f, -profile_.dropInitialSpeed, 0.f};
    execution_->lastTick          = tick;
    execution_->candidate.support = HangSupport::None;
    execution_->compactCollisionActive = false;
    execution_->branchWindowOpen       = false;
    phase_                        = ClimbingPhase::Dropping;
    terminalCode_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::climbUp(eve::SimulationTick tick) {
    if (phase_ != ClimbingPhase::Hanging || !execution_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "climb up requires a hanging execution", "runtime.phase", {}, "climbing"));
    if (tick <= execution_->lastTick)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "climb-up tick must be newer than the last execution tick", "tick", {}, "climbing"));
    const auto found = std::find_if(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
        return (action.kind == ClimbingActionKind::ClimbUp || action.kind == ClimbingActionKind::Mantle) &&
               execution_->candidate.obstacleHeight + epsilon >= action.minHeight &&
               execution_->candidate.obstacleHeight - epsilon <= action.maxHeight;
    });
    if (found == profile_.actions.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no compatible climb-up action is registered", "profile.actions", {}, "climbing"));
    if (execution_->anchorGraph.isValid()) {
        auto released = releaseAnchorReservation();
        if (!released && released.error() && released.error()->code() != eve::DiagnosticCode::StaleHandle &&
            released.error()->code() != eve::DiagnosticCode::NotFound)
            return released;
    }
    execution_->candidate.actionId = found->id;
    execution_->candidate.kind     = ClimbingActionKind::ClimbUp;
    execution_->candidate.definitionGeneration = definitionGeneration_;
    execution_->definitionGeneration           = definitionGeneration_;
    execution_->action             = *found;
    execution_->startFeet          = execution_->currentFeet;
    execution_->candidate.landingFeet =
        execution_->candidate.topPoint - execution_->candidate.surfaceNormal * found->landingForward;
    execution_->elapsed             = eve::Duration::zero();
    execution_->duration            = found->duration;
    execution_->lastTick            = tick;
    execution_->accumulatedResidual = {};
    execution_->leftContactEmitted  = false;
    execution_->rightContactEmitted = false;
    execution_->landContactReleased = false;
    execution_->compactCollisionActive = false;
    execution_->branchWindowOpen       = false;
    phase_                          = ClimbingPhase::Launching;
    terminalCode_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::cancel(ClimbingCancelReason reason, eve::SimulationTick tick) {
    if (!isActivePhase(phase_) || !execution_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "no climbing execution is active", "runtime.phase", {}, "climbing"));
    if (tick < execution_->lastTick)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "cancel tick must not precede the last execution tick", "tick", {}, "climbing"));
    const ClimbingActionDefinition& action = execution_->action;
    const float                     t =
        action.duration.nanoseconds() > 0
            ? static_cast<float>(std::clamp(execution_->elapsed.seconds() / action.duration.seconds(), 0.0, 1.0))
            : 0.f;
    if (phase_ != ClimbingPhase::Hanging && phase_ != ClimbingPhase::Dropping &&
        (t < action.cancelWindowStart || t > action.cancelWindowEnd))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "the active definition does not allow cancellation in this window", "action.cancelWindow", {}, "climbing"));
    auto eventCapacity = requireEventCapacity(1, tick);
    if (!eventCapacity) return eventCapacity;
    if (execution_->anchorGraph.isValid()) {
        auto released = releaseAnchorReservation();
        if (!released && released.error() && released.error()->code() != eve::DiagnosticCode::StaleHandle &&
            released.error()->code() != eve::DiagnosticCode::NotFound)
            return released;
    }
    enqueueEvent({ClimbingEventKind::Cancelled, execution_->candidate.actionId, tick, execution_->executionId});
    terminalCode_ = reason == ClimbingCancelReason::LinkStale            ? "climbing.link.stale"
                    : reason == ClimbingCancelReason::AnchorStale        ? "climbing.anchor.stale"
                    : reason == ClimbingCancelReason::MotionBlocked      ? "climbing.motion.blocked"
                    : reason == ClimbingCancelReason::WarpBudgetExceeded ? "climbing.warp.budget_exceeded"
                                                                         : "climbing.cancelled";
    execution_.reset();
    phase_ = ClimbingPhase::Cancelled;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

ClimbingDebugSnapshot ClimbingRuntime::inspect() const {
    ClimbingDebugSnapshot snapshot;
    snapshot.phase        = phase_;
    snapshot.candidates   = lastCandidates_;
    snapshot.queryCount   = lastQueryCount_;
    snapshot.terminalCode = terminalCode_;
    snapshot.queries       = lastDebugQueries_;
    snapshot.evidence      = lastEvidence_;
    snapshot.motion        = motionEvidence_;
    if (execution_) {
    snapshot.accumulatedWarpResidual = execution_->accumulatedResidual;
        snapshot.executionId             = execution_->executionId;
    }
    snapshot.broadPhaseQueryCount = lastCounters_.broadPhaseQueryCount;
    snapshot.broadPhaseHitCount = lastCounters_.broadPhaseHitCount;
    return snapshot;
}

void ClimbingRuntime::setDebugCapture(ClimbingDebugCapture capture) noexcept {
    debugCapture_ = capture;
    if (capture == ClimbingDebugCapture::Disabled) {
        lastCandidates_.clear();
        lastDebugQueries_.clear();
        lastEvidence_.clear();
        motionEvidence_.clear();
    }
}

void ClimbingRuntime::recordOrdinaryTick(eve::SimulationTick tick, std::uint32_t queryCount,
                                         std::uint32_t moverIterations,
                                         std::uint64_t elapsedNanoseconds) noexcept {
    lastQueryCount_ = queryCount;
    lastCounters_ = {};
    lastCounters_.workload = ClimbingWorkload::Ordinary;
    lastCounters_.queryBudget = ClimbingQueryBudgets::Ordinary;
    lastCounters_.queryCount = queryCount;
    lastCounters_.moverIterations = moverIterations;
    if (queryCount > lastCounters_.queryBudget)
        lastCounters_.budgetState = ClimbingQueryBudgetState::Exceeded;
    telemetry_.record({tick, elapsedNanoseconds, lastCounters_});
}

}  // namespace eve::climbing
