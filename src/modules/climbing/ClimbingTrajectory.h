#pragma once

#include "climbing/Climbing.h"

#include <algorithm>

namespace eve::climbing::detail {

inline float smoothStep(float value) noexcept {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

inline Vec3 interpolate(Vec3 start, Vec3 end, float amount) noexcept {
    return {start.x + (end.x - start.x) * amount, start.y + (end.y - start.y) * amount,
            start.z + (end.z - start.z) * amount};
}

/**
 * Internal deterministic path shared by candidate validation and authoritative execution.
 * Mantles rise clear of the lip before crossing it; vault and ledge approaches retain an arc.
 */
inline Vec3 trajectoryPoint(Vec3 start, const ClimbingCandidate& candidate,
                            const ClimbingActionDefinition& action, const ClimbingProfileDefinition& profile,
                            float normalizedTime) noexcept {
    const float t = std::clamp(normalizedTime, 0.f, 1.f);
    const bool surfaceFollow = action.trajectory == ClimbingTrajectoryKind::SurfaceFollow ||
                               action.trajectory == ClimbingTrajectoryKind::AnchorToAnchor;
    const bool ballistic = action.trajectory == ClimbingTrajectoryKind::BallisticArc;
    if (!ballistic && !surfaceFollow &&
        (action.kind == ClimbingActionKind::Mantle || action.kind == ClimbingActionKind::ClimbUp)) {
        constexpr float riseEnd   = 0.4f;
        constexpr float crossEnd  = 0.85f;
        const float     clearance = std::max({start.y, candidate.topPoint.y + profile.skin,
                                              candidate.landingFeet.y + profile.skin});
        const Vec3      raisedStart{start.x, clearance, start.z};
        const Vec3      raisedLanding{candidate.landingFeet.x, clearance, candidate.landingFeet.z};
        if (t < riseEnd) return interpolate(start, raisedStart, smoothStep(t / riseEnd));
        if (t < crossEnd)
            return interpolate(raisedStart, raisedLanding, smoothStep((t - riseEnd) / (crossEnd - riseEnd)));
        return interpolate(raisedLanding, candidate.landingFeet, smoothStep((t - crossEnd) / (1.f - crossEnd)));
    }

    if (surfaceFollow || action.kind == ClimbingActionKind::Shimmy ||
        action.kind == ClimbingActionKind::CornerInner ||
        action.kind == ClimbingActionKind::CornerOuter || action.kind == ClimbingActionKind::ClimbDown ||
        action.kind == ClimbingActionKind::LadderMount || action.kind == ClimbingActionKind::LadderClimb ||
        action.kind == ClimbingActionKind::LadderDismount || action.kind == ClimbingActionKind::Slide ||
        action.kind == ClimbingActionKind::BeamBalance)
        return interpolate(start, candidate.landingFeet, smoothStep(t));

    Vec3 point = interpolate(start, candidate.landingFeet, t);
    point.y += 4.f * action.apexHeight * t * (1.f - t);
    return point;
}

}  // namespace eve::climbing::detail
