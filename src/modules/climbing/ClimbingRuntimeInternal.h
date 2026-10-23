#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include "climbing/Climbing.h"
namespace eve::climbing::runtime_detail {


constexpr float       epsilon         = 1e-5f;
constexpr std::size_t maxDebugEntries = 64;

/** @brief RuntimeTelemetryScope public API. */
class RuntimeTelemetryScope {
public:
    /** @brief Constructs a RuntimeTelemetryScope. */
    RuntimeTelemetryScope(ClimbingTelemetryBuffer& buffer, ClimbingRuntimeCounters& counters,
                          eve::SimulationTick tick) noexcept
        /** @brief Buffer. */
        : buffer_(buffer), counters_(counters), tick_(tick), start_(std::chrono::steady_clock::now()) {}

    /** @brief Releases RuntimeTelemetryScope resources. */
    ~RuntimeTelemetryScope() {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        buffer_.record(
            {tick_, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
             counters_});
    }

private:
    ClimbingTelemetryBuffer&              buffer_;
    ClimbingRuntimeCounters&              counters_;
    eve::SimulationTick                   tick_;
    std::chrono::steady_clock::time_point start_;
};

template <class T>
/** @brief Bounded debug push. */
void boundedDebugPush(std::vector<T>& values, T value) {
    if (values.size() < maxDebugEntries) values.push_back(std::move(value));
}

/** @brief True when finite. */
inline bool isFinite(float value) { return std::isfinite(value); }

/** @brief True when finite. */
inline bool isFinite(Vec3 value) { return isFinite(value.x) && isFinite(value.y) && isFinite(value.z); }

/** @brief Length squared. */
inline float lengthSquared(Vec3 value) { return value.x * value.x + value.y * value.y + value.z * value.z; }
/** @brief Length. */
inline float length(Vec3 value) { return std::sqrt(lengthSquared(value)); }

/** @brief Operator +. */
inline Vec3 operator+(Vec3 lhs, Vec3 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
/** @brief Operator -. */
inline Vec3 operator-(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
/** @brief Operator *. */
inline Vec3 operator*(Vec3 value, float scale) { return {value.x * scale, value.y * scale, value.z * scale}; }

/** @brief Normalized horizontal. */
inline Vec3 normalizedHorizontal(Vec3 value) {
    value.y            = 0.f;
    const float length = std::sqrt(lengthSquared(value));
    return length > epsilon ? value * (1.f / length) : Vec3{};
}

/** @brief Clamp magnitude. */
inline Vec3 clampMagnitude(Vec3 value, float maximum) {
    const float magnitude = length(value);
    return magnitude > maximum && magnitude > epsilon ? value * (maximum / magnitude) : value;
}

/** @brief WarpChannels public API. */
struct WarpChannels {
    bool horizontal = false;
    bool vertical   = false;
    bool facing     = false;
};

/** @brief Active warp channels. */
inline WarpChannels activeWarpChannels(const ClimbingActionDefinition& action, float normalizedTime) {
    if (action.warpWindows.empty()) return {true, true, true};
    for (const ClimbingWarpWindow& window : action.warpWindows)
        if (normalizedTime + epsilon >= window.start && normalizedTime <= window.end + epsilon)
            return {window.horizontal, window.vertical, window.facing};
    return {};
}

/** @brief Active branch window. */
inline bool activeBranchWindow(const ClimbingActionDefinition& action, float normalizedTime) {
    /** @brief Any of. */
    return std::any_of(action.branchWindows.begin(), action.branchWindows.end(), [&](const auto& window) {
        return normalizedTime + epsilon >= window.start && normalizedTime <= window.end + epsilon;
    });
}

/** @brief Optional combo tag. @borrowed From action; lifetime ends when the caller-owned action changes or is
 * destroyed. */
inline const std::string* activeBranchComboTag(const ClimbingActionDefinition& action, float normalizedTime) {
    const auto found = std::find_if(action.branchWindows.begin(), action.branchWindows.end(), [&](const auto& window) {
        return normalizedTime + epsilon >= window.start && normalizedTime <= window.end + epsilon;
    });
    return found == action.branchWindows.end() ? nullptr : &found->comboTag;
}

/** @brief Active contact weight. */
inline float activeContactWeight(const ClimbingActionDefinition& action, ClimbingContactTarget target,
                                 float normalizedTime) {
    float weight = 0.f;
    for (const ClimbingContactConstraint& constraint : action.contactConstraints) {
        if (constraint.target == target && normalizedTime + epsilon >= constraint.start &&
            normalizedTime <= constraint.end + epsilon)
            weight = std::max(weight, constraint.maxWeight);
    }
    return weight;
}

/** @brief Terminal velocity for. */
inline Vec3 terminalVelocityFor(const ClimbingActionDefinition& action, Vec3 actualDelta, float inverseDelta) {
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

/** @brief Final warp window end. */
inline float finalWarpWindowEnd(const ClimbingActionDefinition& action) {
    return action.warpWindows.empty() ? 1.f : action.warpWindows.back().end;
}

/** @brief Signed horizontal angle. */
inline float signedHorizontalAngle(Vec3 from, Vec3 to) {
    from = normalizedHorizontal(from);
    to   = normalizedHorizontal(to);
    if (lengthSquared(from) <= epsilon || lengthSquared(to) <= epsilon) return 0.f;
    const float crossY = from.z * to.x - from.x * to.z;
    const float dot    = std::clamp(from.x * to.x + from.z * to.z, -1.f, 1.f);
    /** @brief Atan 2. */
    return std::atan2(crossY, dot);
}

/** @brief True when active phase. */
inline bool isActivePhase(ClimbingPhase phase) {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed && phase != ClimbingPhase::Cancelled &&
           phase != ClimbingPhase::Failed;
}

/** @brief True when runtime probe kind. */
inline bool isRuntimeProbeKind(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::Vault || kind == ClimbingActionKind::Mantle ||
           kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::ClimbUp ||
           kind == ClimbingActionKind::WallRun || kind == ClimbingActionKind::Slide;
}

/** @brief True when obstacle probe kind. */
inline bool isObstacleProbeKind(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::Vault || kind == ClimbingActionKind::Mantle ||
           kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::ClimbUp;
}

/** @brief True when anchor hang end. */
inline bool isAnchorHangEnd(ClimbingActionKind kind) {
    return kind == ClimbingActionKind::LedgeGrab || kind == ClimbingActionKind::Shimmy ||
           kind == ClimbingActionKind::CornerInner || kind == ClimbingActionKind::CornerOuter ||
           kind == ClimbingActionKind::LedgeJump || kind == ClimbingActionKind::ClimbDown ||
           kind == ClimbingActionKind::LadderMount || kind == ClimbingActionKind::LadderClimb ||
           kind == ClimbingActionKind::BeamBalance || kind == ClimbingActionKind::PoleSwing ||
           kind == ClimbingActionKind::BarSwing;
}

/** @brief Quantize millimeters. */
inline std::int64_t quantizeMillimeters(float value) {
    const double scaled = std::round(static_cast<double>(value) * 1000.0);
    return static_cast<std::int64_t>(std::clamp(scaled, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                                                static_cast<double>(std::numeric_limits<std::int32_t>::max())));
}

/** @brief Optional action. @borrowed From profile; lifetime ends when the caller-owned profile changes or is destroyed.
 */
inline const ClimbingActionDefinition* findAction(const ClimbingProfile& profile, std::string_view id) {
    const auto found = std::find_if(profile.actions.begin(), profile.actions.end(),
                                    [id](const auto& action) { return action.id == id; });
    return found == profile.actions.end() ? nullptr : &*found;
}

/** @brief True when candidate less. */
inline bool isCandidateLess(const ClimbingCandidate& lhs, const ClimbingCandidate& rhs) {
    if (lhs.score != rhs.score) return lhs.score < rhs.score;
    if (lhs.actionId != rhs.actionId) return lhs.actionId < rhs.actionId;
    if (lhs.obstacleBodyId != rhs.obstacleBodyId) return lhs.obstacleBodyId < rhs.obstacleBodyId;
    return lhs.obstacleShapeId < rhs.obstacleShapeId;
}

/** @brief True if any tag. */
inline bool containsAnyTag(const std::vector<std::string>& actionTags, const std::vector<std::string>& policyTags) {
    /** @brief Any of. */
    return std::any_of(actionTags.begin(), actionTags.end(), [&](const std::string& tag) {
        /** @brief Finds . */
        return std::find(policyTags.begin(), policyTags.end(), tag) != policyTags.end();
    });
}

/** @brief True when action enabled for pose. */
inline bool isActionEnabledForPose(const ClimbingProfile& profile, const ClimbingActionDefinition& action,
                                   const ClimbingPose& pose) {
    if (!profile.defaultActionIds.empty() && std::find(profile.defaultActionIds.begin(), profile.defaultActionIds.end(),
                                                       action.id) == profile.defaultActionIds.end())
        return false;
    if (!profile.allowedActionTags.empty() && !containsAnyTag(action.tags, profile.allowedActionTags)) return false;
    if (containsAnyTag(action.tags, profile.deniedActionTags)) return false;
    const auto required = pose.grounded ? ClimbingSourceMode::Grounded : ClimbingSourceMode::Airborne;
    return (static_cast<std::uint8_t>(action.sourceModes) & static_cast<std::uint8_t>(required)) != 0;
}

/** @brief True when probe recipe matching kind. */
inline bool isProbeRecipeMatchingKind(const ClimbingActionDefinition& action) {
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

/** @brief Parse tag selector. */
inline std::optional<int> parseTagSelector(std::string_view selector, std::string_view prefix) {
    if (!selector.starts_with(prefix)) return std::nullopt;
    const std::string_view digits = selector.substr(prefix.size());
    if (digits.empty()) return std::nullopt;
    int        value  = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return std::nullopt;
    return value;
}

/** @brief True when support selector match. */
inline bool isSupportSelectorMatch(const ClimbingActionDefinition& action, int shapeTag, int materialId) {
    for (const std::string& selector : action.requiredSupportTags) {
        if (const auto expected = parseTagSelector(selector, "shape:")) {
            if (shapeTag != *expected) return false;
        } else if (const auto expected = parseTagSelector(selector, "material:")) {
            if (materialId != *expected) return false;
        } else {
            return false;
        }
    }
    return true;
}

/** @brief Weighted millimeters. */
inline std::int64_t weightedMillimeters(float value, std::int32_t weight) {
    /** @brief Quantize millimeters. */
    return quantizeMillimeters(value) * static_cast<std::int64_t>(weight);
}

/** @brief Selection cost. */
inline std::int64_t selectionCost(const ClimbingProfile& profile, const ClimbingActionDefinition& action,
                                  const ClimbingPose& pose, Vec3 targetDirection, Vec3 targetDelta, float heightError,
                                  float distance, std::int64_t stableTieBreak = 0) {
    const Vec3 forward = normalizedHorizontal(pose.forward);
    targetDirection    = normalizedHorizontal(targetDirection);
    if (lengthSquared(targetDirection) <= epsilon) targetDirection = forward;

    Vec3 intentDirection =
        /** @brief Normalized horizontal. */
        normalizedHorizontal(pose.inputMode == ClimbingInputMode::Precision ? pose.lookIntent : pose.moveIntent);
    if (lengthSquared(intentDirection) <= epsilon) intentDirection = forward;

    const float directionDot = std::clamp(forward.x * targetDirection.x + forward.z * targetDirection.z, -1.f, 1.f);
    const float intentDot =
        /** @brief Clamp. */
        std::clamp(intentDirection.x * targetDirection.x + intentDirection.z * targetDirection.z, -1.f, 1.f);
    const bool  precision            = pose.inputMode == ClimbingInputMode::Precision;
    const float assistScale          = 1.f - profile.autoAssistStrength * (precision ? 0.15f : 0.5f);
    const float directionModeScale   = precision ? 1.5f : 0.75f;
    const float speedModeScale       = precision ? 0.5f : 1.5f;
    const float translationModeScale = precision ? 1.25f : 0.75f;
    const float rotationModeScale    = precision ? 1.5f : 0.75f;
    const float intentModeScale      = precision ? 1.5f : 0.5f;
    const float horizontalDelta      = std::sqrt(targetDelta.x * targetDelta.x + targetDelta.z * targetDelta.z);
    const float warpTranslation      = std::sqrt(horizontalDelta * horizontalDelta + targetDelta.y * targetDelta.y);
    const float warpRotation         = std::acos(directionDot);
    const float intentMismatch       = 1.f - intentDot;

    return static_cast<std::int64_t>(action.selectionBias) * 1000000ll +
           /** @brief Weighted millimeters. */
           weightedMillimeters((1.f - directionDot) * assistScale * directionModeScale,
                               profile.scoreWeights.direction) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(std::fabs(pose.speed - action.minSpeed) * speedModeScale,
                               profile.scoreWeights.approachSpeed) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(heightError, profile.scoreWeights.height) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(distance, profile.scoreWeights.distance) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(warpTranslation * translationModeScale, profile.scoreWeights.warpTranslation) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(warpRotation * rotationModeScale, profile.scoreWeights.warpRotation) +
           /** @brief Weighted millimeters. */
           weightedMillimeters(intentMismatch * intentModeScale, profile.scoreWeights.intentMismatch) + stableTieBreak;
}


}  // namespace eve::climbing::runtime_detail
