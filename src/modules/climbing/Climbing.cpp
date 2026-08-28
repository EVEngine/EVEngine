#include "climbing/Climbing.h"

#include "climbing/ClimbingCodec.h"
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
namespace {

constexpr float epsilon = 1e-5f;
constexpr std::size_t maxDebugEntries = 64;

class RuntimeTelemetryScope {
public:
    RuntimeTelemetryScope(ClimbingTelemetryBuffer& buffer, ClimbingRuntimeCounters& counters,
                          eve::SimulationTick tick) noexcept
        : buffer_(buffer), counters_(counters), tick_(tick), start_(std::chrono::steady_clock::now()) {}

    ~RuntimeTelemetryScope() {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        buffer_.record({tick_, static_cast<std::uint64_t>(
                                   std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
                        counters_});
    }

private:
    ClimbingTelemetryBuffer&              buffer_;
    ClimbingRuntimeCounters&              counters_;
    eve::SimulationTick                  tick_;
    std::chrono::steady_clock::time_point start_;
};

template <class T>
void boundedDebugPush(std::vector<T>& values, T value) {
    if (values.size() < maxDebugEntries) values.push_back(std::move(value));
}

template <class T>
eve::Result<T> climbingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing"));
}

bool finite(float value) { return std::isfinite(value); }

bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }

float lengthSquared(Vec3 value) { return value.x * value.x + value.y * value.y + value.z * value.z; }
float length(Vec3 value) { return std::sqrt(lengthSquared(value)); }

Vec3 operator+(Vec3 lhs, Vec3 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
Vec3 operator-(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
Vec3 operator*(Vec3 value, float scale) { return {value.x * scale, value.y * scale, value.z * scale}; }

Vec3 normalizedHorizontal(Vec3 value) {
    value.y            = 0.f;
    const float length = std::sqrt(lengthSquared(value));
    return length > epsilon ? value * (1.f / length) : Vec3{};
}

Vec3 clampMagnitude(Vec3 value, float maximum) {
    const float magnitude = length(value);
    return magnitude > maximum && magnitude > epsilon ? value * (maximum / magnitude) : value;
}

struct WarpChannels {
    bool horizontal = false;
    bool vertical   = false;
    bool facing     = false;
};

WarpChannels activeWarpChannels(const ClimbingActionDefinition& action, float normalizedTime) {
    if (action.warpWindows.empty()) return {true, true, true};
    for (const ClimbingWarpWindow& window : action.warpWindows)
        if (normalizedTime + epsilon >= window.start && normalizedTime <= window.end + epsilon)
            return {window.horizontal, window.vertical, window.facing};
    return {};
}

bool activeBranchWindow(const ClimbingActionDefinition& action, float normalizedTime) {
    return std::any_of(action.branchWindows.begin(), action.branchWindows.end(), [&](const auto& window) {
        return normalizedTime + epsilon >= window.start && normalizedTime <= window.end + epsilon;
    });
}

const std::string* activeBranchComboTag(const ClimbingActionDefinition& action, float normalizedTime) {
    const auto found = std::find_if(action.branchWindows.begin(), action.branchWindows.end(),
                                    [&](const auto& window) {
                                        return normalizedTime + epsilon >= window.start &&
                                               normalizedTime <= window.end + epsilon;
                                    });
    return found == action.branchWindows.end() ? nullptr : &found->comboTag;
}

float activeContactWeight(const ClimbingActionDefinition& action, ClimbingContactTarget target,
                          float normalizedTime) {
    float weight = 0.f;
    for (const ClimbingContactConstraint& constraint : action.contactConstraints) {
        if (constraint.target == target && normalizedTime + epsilon >= constraint.start &&
            normalizedTime <= constraint.end + epsilon)
            weight = std::max(weight, constraint.maxWeight);
    }
    return weight;
}

Vec3 terminalVelocityFor(const ClimbingActionDefinition& action, Vec3 actualDelta, float inverseDelta) {
    Vec3 velocity = actualDelta * inverseDelta;
    switch (action.landingPolicy) {
        case ClimbingLandingPolicy::PreserveMomentum: break;
        case ClimbingLandingPolicy::MatchGround: velocity.y = 0.f; break;
        case ClimbingLandingPolicy::Stop: velocity = {}; break;
    }
    switch (action.terminalVelocityPolicy) {
        case ClimbingTerminalVelocityPolicy::Preserve: break;
        case ClimbingTerminalVelocityPolicy::ClampDownward: velocity.y = std::min(velocity.y, 0.f); break;
        case ClimbingTerminalVelocityPolicy::Zero: velocity.y = 0.f; break;
    }
    return velocity;
}

float finalWarpWindowEnd(const ClimbingActionDefinition& action) {
    return action.warpWindows.empty() ? 1.f : action.warpWindows.back().end;
}

float signedHorizontalAngle(Vec3 from, Vec3 to) {
    from = normalizedHorizontal(from);
    to   = normalizedHorizontal(to);
    if (lengthSquared(from) <= epsilon || lengthSquared(to) <= epsilon) return 0.f;
    const float crossY = from.z * to.x - from.x * to.z;
    const float dot    = std::clamp(from.x * to.x + from.z * to.z, -1.f, 1.f);
    return std::atan2(crossY, dot);
}

bool isActivePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed && phase != ClimbingPhase::Cancelled &&
           phase != ClimbingPhase::Failed;
}

bool isRuntimeProbeKind(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::Vault || kind == ClimbingActionKind::Mantle ||
           kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::ClimbUp ||
           kind == ClimbingActionKind::WallRun || kind == ClimbingActionKind::Slide;
}

bool isObstacleProbeKind(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::Vault || kind == ClimbingActionKind::Mantle ||
           kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::ClimbUp;
}

bool endsAtAnchorHang(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::Shimmy ||
           kind == ClimbingActionKind::CornerInner || kind == ClimbingActionKind::CornerOuter ||
           kind == ClimbingActionKind::LedgeJump || kind == ClimbingActionKind::ClimbDown ||
           kind == ClimbingActionKind::LadderMount || kind == ClimbingActionKind::LadderClimb ||
           kind == ClimbingActionKind::BeamBalance || kind == ClimbingActionKind::PoleSwing ||
           kind == ClimbingActionKind::BarSwing;
}

std::int64_t quantizeMillimeters(float value) {
    const double scaled = std::round(static_cast<double>(value) * 1000.0);
    return static_cast<std::int64_t>(std::clamp(scaled, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                                                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
}

const ClimbingActionDefinition* findAction(const ClimbingProfile& profile, std::string_view id) {
    const auto found = std::find_if(profile.actions.begin(), profile.actions.end(),
                                    [id](const auto& action) { return action.id == id; });
    return found == profile.actions.end() ? nullptr : &*found;
}

bool candidateLess(const ClimbingCandidate& lhs, const ClimbingCandidate& rhs) {
    if (lhs.score != rhs.score) return lhs.score < rhs.score;
    if (lhs.actionId != rhs.actionId) return lhs.actionId < rhs.actionId;
    if (lhs.obstacleBodyId != rhs.obstacleBodyId) return lhs.obstacleBodyId < rhs.obstacleBodyId;
    return lhs.obstacleShapeId < rhs.obstacleShapeId;
}

bool containsAnyTag(const std::vector<std::string>& actionTags, const std::vector<std::string>& policyTags) {
    return std::any_of(actionTags.begin(), actionTags.end(), [&](const std::string& tag) {
        return std::find(policyTags.begin(), policyTags.end(), tag) != policyTags.end();
    });
}

bool actionEnabledForPose(const ClimbingProfile& profile, const ClimbingActionDefinition& action,
                          const ClimbingPose& pose) {
    if (!profile.defaultActionIds.empty() &&
        std::find(profile.defaultActionIds.begin(), profile.defaultActionIds.end(), action.id) ==
            profile.defaultActionIds.end())
        return false;
    if (!profile.allowedActionTags.empty() && !containsAnyTag(action.tags, profile.allowedActionTags)) return false;
    if (containsAnyTag(action.tags, profile.deniedActionTags)) return false;
    const auto required = pose.grounded ? ClimbingSourceMode::Grounded : ClimbingSourceMode::Airborne;
    return (static_cast<std::uint8_t>(action.sourceModes) & static_cast<std::uint8_t>(required)) != 0;
}

bool probeRecipeMatchesKind(const ClimbingActionDefinition& action) {
    switch (action.probeRecipe) {
        case ClimbingProbeRecipe::Automatic: return true;
        case ClimbingProbeRecipe::Obstacle:
            return action.kind == ClimbingActionKind::Vault || action.kind == ClimbingActionKind::Mantle;
        case ClimbingProbeRecipe::Ledge:
            return action.kind == ClimbingActionKind::LedgeGrab || action.kind == ClimbingActionKind::ClimbUp;
        case ClimbingProbeRecipe::Wall: return action.kind == ClimbingActionKind::WallRun;
        case ClimbingProbeRecipe::Ground: return action.kind == ClimbingActionKind::Slide;
        case ClimbingProbeRecipe::AnchorGraph: return !isRuntimeProbeKind(action.kind);
    }
    return false;
}

bool parseTagSelector(std::string_view selector, std::string_view prefix, int& value) {
    if (!selector.starts_with(prefix)) return false;
    const std::string_view digits = selector.substr(prefix.size());
    if (digits.empty()) return false;
    const char* begin = digits.data();
    const char* end = begin + digits.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool supportSelectorsMatch(const ClimbingActionDefinition& action, int shapeTag, int materialId) {
    for (const std::string& selector : action.requiredSupportTags) {
        int expected = 0;
        if (parseTagSelector(selector, "shape:", expected)) {
            if (shapeTag != expected) return false;
        } else if (parseTagSelector(selector, "material:", expected)) {
            if (materialId != expected) return false;
        } else {
            return false;
        }
    }
    return true;
}

std::int64_t weightedMillimeters(float value, std::int32_t weight) {
    return quantizeMillimeters(value) * static_cast<std::int64_t>(weight);
}

std::int64_t selectionCost(const ClimbingProfile& profile, const ClimbingActionDefinition& action,
                           const ClimbingPose& pose, Vec3 targetDirection, Vec3 targetDelta,
                           float heightError, float distance, std::int64_t stableTieBreak = 0) {
    const Vec3 forward = normalizedHorizontal(pose.forward);
    targetDirection    = normalizedHorizontal(targetDirection);
    if (lengthSquared(targetDirection) <= epsilon) targetDirection = forward;

    Vec3 intentDirection = normalizedHorizontal(pose.inputMode == ClimbingInputMode::Precision
                                                    ? pose.lookIntent
                                                    : pose.moveIntent);
    if (lengthSquared(intentDirection) <= epsilon) intentDirection = forward;

    const float directionDot = std::clamp(forward.x * targetDirection.x + forward.z * targetDirection.z,
                                          -1.f, 1.f);
    const float intentDot = std::clamp(intentDirection.x * targetDirection.x +
                                           intentDirection.z * targetDirection.z,
                                       -1.f, 1.f);
    const bool precision = pose.inputMode == ClimbingInputMode::Precision;
    const float assistScale = 1.f - profile.autoAssistStrength * (precision ? 0.15f : 0.5f);
    const float directionModeScale = precision ? 1.5f : 0.75f;
    const float speedModeScale = precision ? 0.5f : 1.5f;
    const float translationModeScale = precision ? 1.25f : 0.75f;
    const float rotationModeScale = precision ? 1.5f : 0.75f;
    const float intentModeScale = precision ? 1.5f : 0.5f;
    const float horizontalDelta = std::sqrt(targetDelta.x * targetDelta.x + targetDelta.z * targetDelta.z);
    const float warpTranslation = std::sqrt(horizontalDelta * horizontalDelta + targetDelta.y * targetDelta.y);
    const float warpRotation = std::acos(directionDot);
    const float intentMismatch = 1.f - intentDot;

    return static_cast<std::int64_t>(action.selectionBias) * 1000000ll +
           weightedMillimeters((1.f - directionDot) * assistScale * directionModeScale,
                               profile.scoreWeights.direction) +
           weightedMillimeters(std::fabs(pose.speed - action.minSpeed) * speedModeScale,
                               profile.scoreWeights.approachSpeed) +
           weightedMillimeters(heightError, profile.scoreWeights.height) +
           weightedMillimeters(distance, profile.scoreWeights.distance) +
           weightedMillimeters(warpTranslation * translationModeScale,
                               profile.scoreWeights.warpTranslation) +
           weightedMillimeters(warpRotation * rotationModeScale, profile.scoreWeights.warpRotation) +
           weightedMillimeters(intentMismatch * intentModeScale, profile.scoreWeights.intentMismatch) +
           stableTieBreak;
}

}  // namespace

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
    auto worst = std::max_element(values_.begin(), values_.end(), candidateLess);
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
    std::sort(values_.begin(), values_.begin() + static_cast<std::ptrdiff_t>(size_), candidateLess);
    size_ = std::min(size_, std::min(limit, Capacity));
}

void ClimbingCandidateSet::swap(ClimbingCandidateSet& other) noexcept {
    values_.swap(other.values_);
    std::swap(size_, other.size_);
}

eve::Result<void> ClimbingRuntime::requireEventCapacity(std::size_t count, eve::SimulationTick tick) const {
    if (count > PendingEventCapacity - pendingEvents_.size())
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "climbing event queue must be drained before simulation can continue",
                                     "runtime.pendingEvents");
    if (!pendingEvents_.empty() && tick < pendingEvents_.back().tick)
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "climbing event tick must not precede an undelivered event",
                                     "runtime.pendingEvents.tick");
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
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "profile cannot change while an execution is active", "runtime.phase");
    if (definitionGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "climbing definition generation is exhausted", "runtime.definitionGeneration");
    std::sort(profile.actions.begin(), profile.actions.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    const auto duplicate = std::adjacent_find(profile.actions.begin(), profile.actions.end(),
                                              [](const auto& lhs, const auto& rhs) { return lhs.id == rhs.id; });
    if (duplicate != profile.actions.end())
        return climbingFailure<void>(eve::DiagnosticCode::Conflict, "profile contains duplicate action ids",
                                     "profile.actions");
    profile_ = std::move(profile);
    validatedAnimationActions_.clear();
    ++definitionGeneration_;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ClimbingRuntime::upsertAction(ClimbingActionDefinition action) {
    auto valid = validateClimbingActionDefinition(action);
    if (!valid) return eve::Result<void>::failure(valid.status());
    if (definitionGeneration_ == std::numeric_limits<std::uint64_t>::max())
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "climbing definition generation is exhausted", "runtime.definitionGeneration");
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
        return climbingFailure<void>(eve::DiagnosticCode::NotFound, "animation binding references an unknown action",
                                     "actionId");
    if (!action->animation.clipId.empty() && clip.getName() != action->animation.clipId)
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "animation clip does not match the action binding", "action.animation.clipId");
    auto validated = clip.validateNotifyContract(action->requiredNotifies);
    if (!validated)
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "climbing.animation.notify_missing: " + validated.status().describe(),
                                     "action.requiredNotifies");
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
        return climbingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                     "physics world is no longer valid", "world");
    if (!finite(pose.feet) || !finite(pose.forward) || !finite(pose.speed) || pose.speed < 0.f ||
        !finite(pose.verticalSpeed) || !finite(pose.moveIntent) || !finite(pose.lookIntent))
        return climbingFailure<void>(
            eve::DiagnosticCode::InvalidArgument, "pose values must be finite and speed non-negative", "pose");
    const Vec3 forward = normalizedHorizontal(pose.forward);
    if (lengthSquared(forward) <= epsilon)
        return climbingFailure<void>(
            eve::DiagnosticCode::InvalidArgument, "pose forward must have a horizontal direction", "pose.forward");

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
        return action.kind == ClimbingActionKind::Slide && actionEnabledForPose(profile_, action, pose) &&
               probeRecipeMatchesKind(action);
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
                if (action.kind != ClimbingActionKind::Slide || !actionEnabledForPose(profile_, action, pose) ||
                    !probeRecipeMatchesKind(action))
                    continue;
                if (pose.speed + epsilon < action.minSpeed) {
                    rejectCandidate();
                    if (debugCapture_ == ClimbingDebugCapture::Enabled)
                        boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "below_min_speed"});
                    continue;
                }
                if (!supportSelectorsMatch(action, ground.shapeTag, ground.materialId)) {
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
        return action.kind == ClimbingActionKind::WallRun && actionEnabledForPose(profile_, action, pose) &&
               probeRecipeMatchesKind(action);
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
                if (action.kind != ClimbingActionKind::WallRun || !actionEnabledForPose(profile_, action, pose) ||
                    !probeRecipeMatchesKind(action) || pose.speed + epsilon < action.minSpeed)
                    continue;
                if (!supportSelectorsMatch(action, hit.shapeTag, hit.materialId)) {
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

    const bool hasObstacleProbe = std::any_of(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
        return isObstacleProbeKind(action.kind) && actionEnabledForPose(profile_, action, pose) &&
               probeRecipeMatchesKind(action);
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
        if (!isObstacleProbeKind(action.kind) || !actionEnabledForPose(profile_, action, pose) ||
            !probeRecipeMatchesKind(action))
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
        if (!actionEnabledForPose(profile_, action, pose)) {
            rejectCandidate();
            if (debugCapture_ == ClimbingDebugCapture::Enabled)
                boundedDebugPush(lastEvidence_, ClimbingCandidateEvidence{action.id, "definition_policy_rejected"});
            continue;
        }
        if (!probeRecipeMatchesKind(action)) {
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
            !supportSelectorsMatch(action, top.shapeTag, top.materialId)) {
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

eve::Result<ClimbingAdvance> ClimbingRuntime::advance(physics::World3D& world, eve::SimulationStep step) {
    return advance(world, step, {});
}

eve::Result<ClimbingAdvance> ClimbingRuntime::advance(physics::World3D& world, eve::SimulationStep step,
                                                      const ClimbingMotionInput& motion) {
    lastQueryCount_ = 0;
    lastCounters_ = {};
    lastCounters_.workload = ClimbingWorkload::Active;
    lastCounters_.queryBudget = ClimbingQueryBudgets::Active;
    RuntimeTelemetryScope telemetryScope(telemetry_, lastCounters_, step.tick);
    if (!isActivePhase(phase_) || !execution_)
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::PreconditionViolation,
                                                "no climbing execution is active", "runtime.phase");
    Execution& execution = *execution_;
    lastCounters_.selectedCost = execution.candidate.score;
    if (world.runtimeHandle() != execution.candidate.world)
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::StaleHandle,
                                                "execution belongs to another or stale physics world", "world");
    if (step.tick <= execution.lastTick)
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::Conflict,
                                                "simulation tick must increase exactly once per update", "step.tick");
    if (step.delta.nanoseconds() <= 0)
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::InvalidArgument,
                                                "simulation delta must be positive", "step.delta");
    if (!finite(motion.rootTranslation) || !finite(motion.facing) || !finite(motion.pelvisOffset) ||
        !finite(motion.rootYawRadians) || length(motion.pelvisOffset) > profile_.maxPelvisDeviation + epsilon)
        return climbingFailure<ClimbingAdvance>(
            eve::DiagnosticCode::InvalidArgument,
            "authored motion must be finite and pelvis deviation must remain inside the profile limit", "motion");
    auto eventCapacity = requireEventCapacity(4, step.tick);
    if (!eventCapacity) return eve::Result<ClimbingAdvance>::failure(eventCapacity.status());

    const bool graphBound = execution.anchorGraph.isValid() && !execution.anchorReservation.id.isZero();
    float graphPointSpeed = -1.f;
    if (graphBound) {
        auto graph = Climbing::resolveAnchorGraph(execution.anchorGraph);
        auto resolved = graph.isBound()
                            ? graph->resolveNodeKinematics(world, execution.anchorNode)
                            : climbingFailure<ResolvedClimbingAnchorNode>(
                                  eve::DiagnosticCode::StaleHandle, "anchor graph instance handle is stale",
                                  "execution.anchorGraph");
        auto reservation = graph.isBound()
                               ? graph->validateReservation(execution.anchorReservation)
                               : climbingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                       "anchor graph instance handle is stale",
                                                       "execution.anchorGraph");
        if (!resolved || !reservation) {
            enqueueEvent(
                {ClimbingEventKind::Cancelled, execution.candidate.actionId, step.tick, execution.executionId});
            phase_ = ClimbingPhase::Cancelled;
            terminalCode_ = "climbing.anchor.stale";
            if (graph.isBound() && reservation) {
                auto released = graph->release(execution.anchorReservation);
                released.ignore("stale graph anchor resolution releases live occupancy");
            }
            execution_.reset();
            return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::StaleHandle,
                                                    "graph anchor or occupancy reservation is stale",
                                                    "execution.anchor");
        }
        execution.candidate.obstacleBody = resolved.value().body;
        execution.candidate.topPoint = resolved.value().position;
        execution.candidate.frontPoint = resolved.value().position;
        execution.candidate.surfaceNormal = resolved.value().normal;
        execution.candidate.surfaceTangent = resolved.value().tangent;
        execution.candidate.leftHandAnchor = resolved.value().leftHandSocket;
        execution.candidate.rightHandAnchor = resolved.value().rightHandSocket;
        if (execution.action.kind == ClimbingActionKind::LadderDismount ||
            execution.action.kind == ClimbingActionKind::BeamBalance) {
            execution.candidate.landingFeet = resolved.value().position;
        } else {
            execution.candidate.landingFeet =
                resolved.value().position +
                resolved.value().normal *
                    (profile_.capsuleRadius + profile_.skin + execution.action.hangBodyOffset);
            execution.candidate.landingFeet.y =
                resolved.value().position.y - execution.action.hangFeetBelowLedge;
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
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::StaleHandle, "climbing target handle is stale",
                                                "execution.candidate.obstacleBody");
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
        return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::PreconditionViolation,
                                                "climbing target exceeded the platform speed limit",
                                                "execution.candidate.obstacleBody");
    }
    if (!graphBound) {
        auto worldTop = obstacle->localToWorldPointOwned(execution.candidate.bodyLocalTop.x,
                                                         execution.candidate.bodyLocalTop.y,
                                                         execution.candidate.bodyLocalTop.z);
        if (!worldTop) return eve::Result<ClimbingAdvance>::failure(worldTop.status());
        auto worldLanding = obstacle->localToWorldPointOwned(
            execution.candidate.bodyLocalLanding.x, execution.candidate.bodyLocalLanding.y,
            execution.candidate.bodyLocalLanding.z);
        if (!worldLanding) return eve::Result<ClimbingAdvance>::failure(worldLanding.status());
        execution.candidate.topPoint = {worldTop.value().x, worldTop.value().y, worldTop.value().z};
        if (execution.candidate.kind == ClimbingActionKind::ClimbUp) {
            execution.candidate.landingFeet =
                execution.candidate.topPoint - execution.candidate.surfaceNormal * execution.action.landingForward;
        } else {
            execution.candidate.landingFeet = {worldLanding.value().x, worldLanding.value().y,
                                                worldLanding.value().z};
        }
        execution.candidate.leftHandAnchor =
            execution.candidate.topPoint - execution.candidate.surfaceTangent * (execution.action.handSpacing * 0.5f);
        execution.candidate.rightHandAnchor =
            execution.candidate.topPoint + execution.candidate.surfaceTangent * (execution.action.handSpacing * 0.5f);
    }

    physics::QueryFilter3D filter = profile_.queryFilter;
    filter.ignoredBodyId          = execution.candidate.ignoredBodyId;
    if (phase_ == ClimbingPhase::Hanging || phase_ == ClimbingPhase::Balanced ||
        phase_ == ClimbingPhase::Swinging) {
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
            return climbingFailure<ClimbingAdvance>(eve::DiagnosticCode::PreconditionViolation,
                                                    "hanging capsule clearance became blocked", "execution.clearance");
        }
        execution.lastTick = step.tick;
        ClimbingAdvance output{phase_,
                               execution.candidate.actionId,
                               execution.currentFeet,
                               {},
                               {},
                               {},
                               1.f,
                               false,
                               false,
                               execution.candidate.support};
        output.leftHandAnchor  = execution.candidate.leftHandAnchor;
        output.rightHandAnchor = execution.candidate.rightHandAnchor;
        const bool handHold = phase_ != ClimbingPhase::Balanced;
        const bool authoredContacts = !execution.action.contactConstraints.empty();
        output.leftHandWeight = authoredContacts
                                    ? activeContactWeight(execution.action, ClimbingContactTarget::LeftHand, 1.f)
                                    : (handHold ? 1.f : 0.f);
        output.rightHandWeight = authoredContacts
                                     ? activeContactWeight(execution.action, ClimbingContactTarget::RightHand, 1.f)
                                     : (handHold ? 1.f : 0.f);
        output.leftFootWeight = activeContactWeight(execution.action, ClimbingContactTarget::LeftFoot, 1.f);
        output.rightFootWeight = activeContactWeight(execution.action, ClimbingContactTarget::RightFoot, 1.f);
        output.pelvisWeight = activeContactWeight(execution.action, ClimbingContactTarget::Pelvis, 1.f);
        output.contactWeight = std::max({output.leftHandWeight, output.rightHandWeight, output.leftFootWeight,
                                         output.rightFootWeight, output.pelvisWeight});
        output.executionId     = execution.executionId;
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
        lastCounters_.queryCount = lastQueryCount_;
        const physics::CapsuleMove3D movement = std::move(moved).takeValue();
        lastCounters_.moverIterations = static_cast<std::uint32_t>(std::max(0, movement.iterations));
        const Vec3                   actual{movement.deltaX, movement.deltaY, movement.deltaZ};
        lastCounters_.warpResidual = desired - actual;
        execution.currentFeet = execution.currentFeet + actual;
        execution.lastTick    = step.tick;
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
        output.executionId = execution.executionId;
        output.cameraCueProfile = profile_.cameraCueProfile;
        output.cameraCue        = execution.action.cameraCue;
        output.animationClipId = execution.action.animation.clipId;
        output.animationGraphNodeId = execution.action.animation.graphNodeId;
        output.animationMirrored = execution.action.animation.mirrored;
        if (movement.grounded) {
            phase_        = ClimbingPhase::Completed;
            terminalCode_ = "climbing.completed";
            output.phase  = phase_;
            enqueueEvent({ClimbingEventKind::Landed, output.actionId, step.tick, execution.executionId});
            enqueueEvent({ClimbingEventKind::Completed, output.actionId, step.tick, execution.executionId});
            output.terminalVelocity = terminalVelocityFor(execution.action, actual, 1.f / dt);
            output.hasTerminalVelocity = true;
            execution_.reset();
        }
        return eve::Result<ClimbingAdvance>::success(std::move(output));
    }

    auto       elapsed  = execution.elapsed.tryAdd(step.delta);
    if (!elapsed) return eve::Result<ClimbingAdvance>::failure(elapsed.status());
    execution.elapsed            = std::move(elapsed).takeValue();
    execution.lastTick           = step.tick;
    const double durationSeconds = execution.duration.seconds();
    const float  t       = static_cast<float>(std::clamp(execution.elapsed.seconds() / durationSeconds, 0.0, 1.0));
    const Vec3 planned =
        detail::trajectoryPoint(execution.startFeet, execution.candidate, execution.action, profile_, t);
    const Vec3 proceduralDelta = planned - execution.currentFeet;
    Vec3       baseDelta       = proceduralDelta;
    if (motion.hasRootMotion) {
        const float authoredLength = length(motion.rootTranslation);
        const float targetLength   = length(proceduralDelta);
        const float scale = authoredLength > epsilon
                                ? std::clamp(targetLength / authoredLength, execution.action.rootMotionScaleMin,
                                             execution.action.rootMotionScaleMax)
                                : execution.action.rootMotionScaleMin;
        baseDelta         = motion.rootTranslation * scale;
    }

    const WarpChannels channels  = activeWarpChannels(execution.action, t);
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
    const float authoredLeftHand = activeContactWeight(execution.action, ClimbingContactTarget::LeftHand, t);
    const float authoredRightHand = activeContactWeight(execution.action, ClimbingContactTarget::RightHand, t);
    bool leftContact = authoredLeftHand > epsilon;
    bool rightContact = authoredRightHand > epsilon;
    bool landContact = false;
    bool compactRequested = false;
    bool compactForTick = execution.compactCollisionActive;
    bool branchForTick = execution.action.branchWindows.empty() ? execution.branchWindowOpen
                                                                 : activeBranchWindow(execution.action, t);
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
                landContact   = true;
                compactForTick = false;
                branchForTick  = false;
                break;
        }
    }
    const float collisionHeight = compactForTick ? profile_.compactCapsuleHeight : profile_.capsuleHeight;
    const float lowerY = execution.currentFeet.y + profile_.capsuleRadius;
    const float upperY = execution.currentFeet.y + collisionHeight - profile_.capsuleRadius;
    auto        moved  = world.moveCapsuleOwned(execution.currentFeet.x, lowerY, execution.currentFeet.z,
                                                execution.currentFeet.x, upperY, execution.currentFeet.z,
                                                profile_.capsuleRadius, desired.x, desired.y, desired.z, filter);
    if (!moved) return eve::Result<ClimbingAdvance>::failure(moved.status());
    ++lastQueryCount_;
    lastCounters_.queryCount = lastQueryCount_;
    const physics::CapsuleMove3D movement = std::move(moved).takeValue();
    lastCounters_.moverIterations = static_cast<std::uint32_t>(std::max(0, movement.iterations));
    const Vec3                   actual{movement.deltaX, movement.deltaY, movement.deltaZ};
    execution.currentFeet         = execution.currentFeet + actual;
    execution.lastPlannedFeet     = planned;
    const Vec3 residual           = desired - actual;
    lastCounters_.warpResidual    = residual;
    execution.accumulatedResidual = execution.accumulatedResidual + residual;
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
    output.executionId     = execution.executionId;
    output.appliedWarp     = appliedWarp;
    output.desiredYawDelta = desiredYawDelta;
    output.cameraCueProfile = profile_.cameraCueProfile;
    output.cameraCue        = execution.action.cameraCue;
    output.animationClipId = execution.action.animation.clipId;
    output.animationGraphNodeId = execution.action.animation.graphNodeId;
    output.animationMirrored = execution.action.animation.mirrored;
    output.leftHandAnchor  = execution.candidate.leftHandAnchor;
    output.rightHandAnchor = execution.candidate.rightHandAnchor;
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
        output.leftHandWeight = execution.leftContactEmitted && !execution.landContactReleased ? 1.f : 0.f;
        output.rightHandWeight = execution.rightContactEmitted && !execution.landContactReleased ? 1.f : 0.f;
    } else if (!execution.landContactReleased) {
        output.leftHandWeight = authoredLeftHand;
        output.rightHandWeight = authoredRightHand;
        output.leftFootWeight = activeContactWeight(execution.action, ClimbingContactTarget::LeftFoot, t);
        output.rightFootWeight = activeContactWeight(execution.action, ClimbingContactTarget::RightFoot, t);
        output.pelvisWeight = activeContactWeight(execution.action, ClimbingContactTarget::Pelvis, t);
    }
    output.contactWeight = std::max({output.leftHandWeight, output.rightHandWeight, output.leftFootWeight,
                                     output.rightFootWeight, output.pelvisWeight});
    output.compactCollisionRequested = compactRequested;
    output.compactCollisionActive    = execution.compactCollisionActive;
    output.branchWindowOpen          = execution.branchWindowOpen;
    if (const std::string* comboTag = activeBranchComboTag(execution.action, t))
        output.branchComboTag = *comboTag;
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
    output.phase = phase_;
    const Vec3 remainingTargetError = planned - execution.currentFeet;
    const bool authoredWarpMissed   = motion.hasRootMotion && t + epsilon >= finalWarpWindowEnd(execution.action) &&
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
        if (graphBound && endsAtAnchorHang(execution.candidate.kind)) {
            if (execution.candidate.kind == ClimbingActionKind::BeamBalance)
                phase_ = ClimbingPhase::Balanced;
            else if (execution.candidate.kind == ClimbingActionKind::PoleSwing ||
                     execution.candidate.kind == ClimbingActionKind::BarSwing)
                phase_ = ClimbingPhase::Swinging;
            else
                phase_ = ClimbingPhase::Hanging;
            execution.branchWindowOpen = true;
            output.branchWindowOpen = true;
            enqueueEvent({execution.candidate.kind == ClimbingActionKind::LedgeGrab
                              ? ClimbingEventKind::Hanging
                              : ClimbingEventKind::AnchorReached,
                          output.actionId, step.tick, execution.executionId});
        } else if (execution.candidate.kind == ClimbingActionKind::LedgeGrab) {
            phase_ = ClimbingPhase::Hanging;
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
            output.terminalVelocity = terminalVelocityFor(execution.action, actual,
                                                           static_cast<float>(1.0 / step.delta.seconds()));
            output.hasTerminalVelocity = true;
            execution_.reset();
        }
    }
    return eve::Result<ClimbingAdvance>::success(std::move(output));
}

eve::Result<void> ClimbingRuntime::drop(eve::SimulationTick tick) {
    if (phase_ != ClimbingPhase::Hanging || !execution_)
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation, "drop requires a hanging execution",
                                     "runtime.phase");
    if (tick <= execution_->lastTick)
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "drop tick must be newer than the last execution tick", "tick");
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
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "climb up requires a hanging execution", "runtime.phase");
    if (tick <= execution_->lastTick)
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "climb-up tick must be newer than the last execution tick", "tick");
    const auto found = std::find_if(profile_.actions.begin(), profile_.actions.end(), [&](const auto& action) {
        return (action.kind == ClimbingActionKind::ClimbUp || action.kind == ClimbingActionKind::Mantle) &&
               execution_->candidate.obstacleHeight + epsilon >= action.minHeight &&
               execution_->candidate.obstacleHeight - epsilon <= action.maxHeight;
    });
    if (found == profile_.actions.end())
        return climbingFailure<void>(eve::DiagnosticCode::NotFound, "no compatible climb-up action is registered",
                                     "profile.actions");
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
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation, "no climbing execution is active",
                                     "runtime.phase");
    if (tick < execution_->lastTick)
        return climbingFailure<void>(eve::DiagnosticCode::Conflict,
                                     "cancel tick must not precede the last execution tick", "tick");
    const ClimbingActionDefinition& action = execution_->action;
    const float                     t =
        action.duration.nanoseconds() > 0
            ? static_cast<float>(std::clamp(execution_->elapsed.seconds() / action.duration.seconds(), 0.0, 1.0))
            : 0.f;
    if (phase_ != ClimbingPhase::Hanging && phase_ != ClimbingPhase::Dropping &&
        (t < action.cancelWindowStart || t > action.cancelWindowEnd))
        return climbingFailure<void>(eve::DiagnosticCode::PreconditionViolation,
                                     "the active definition does not allow cancellation in this window",
                                     "action.cancelWindow");
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
