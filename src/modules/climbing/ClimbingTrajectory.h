#pragma once

#include "climbing/Climbing.h"

#include <algorithm>
#include <cmath>

namespace eve::climbing::detail {

inline float smoothStep(float value) noexcept {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

inline Vec3 interpolate(Vec3 start, Vec3 end, float amount) noexcept {
    return {start.x + (end.x - start.x) * amount, start.y + (end.y - start.y) * amount,
            start.z + (end.z - start.z) * amount};
}

// Join the two support planes in the horizontal plane. Each eased leg stays
// on its own support plane; a direct chord can cut through a convex corner.
inline Vec3 cornerPathPoint(Vec3 start, Vec3 end, Vec3 sourceNormal, Vec3 targetNormal, float normalizedTime) noexcept {
    const float determinant = sourceNormal.x * targetNormal.z - sourceNormal.z * targetNormal.x;
    const float t           = std::clamp(normalizedTime, 0.f, 1.f);
    if (std::abs(determinant) < 1e-4f) return interpolate(start, end, smoothStep(t));
    const float sourcePlane = sourceNormal.x * start.x + sourceNormal.z * start.z;
    const float targetPlane = targetNormal.x * end.x + targetNormal.z * end.z;
    const Vec3  join{(sourcePlane * targetNormal.z - sourceNormal.z * targetPlane) / determinant,
                    (start.y + end.y) * 0.5f,
                    (sourceNormal.x * targetPlane - sourcePlane * targetNormal.x) / determinant};
    return t < 0.5f ? interpolate(start, join, smoothStep(t * 2.f))
                    : interpolate(join, end, smoothStep((t - 0.5f) * 2.f));
}

/**
 * Internal deterministic path shared by candidate validation and authoritative execution.
 * Mantles and wall-contact vaults rise clear of the lip before crossing it.
 * Vaults with takeoff clearance and ledge
 * approaches retain an arc.
 */
inline Vec3 trajectoryPoint(Vec3 start, const ClimbingCandidate& candidate, const ClimbingActionDefinition& action,
                            const ClimbingProfileDefinition& profile, float normalizedTime,
                            Vec3 sourceNormal = {}) noexcept {
    const float t = std::clamp(normalizedTime, 0.f, 1.f);
    if (action.kind == ClimbingActionKind::CornerInner || action.kind == ClimbingActionKind::CornerOuter)
        return cornerPathPoint(start, candidate.landingFeet, sourceNormal, candidate.surfaceNormal, t);
    const bool surfaceFollow = action.trajectory == ClimbingTrajectoryKind::SurfaceFollow ||
                               action.trajectory == ClimbingTrajectoryKind::AnchorToAnchor;
    const bool ballistic = action.trajectory == ClimbingTrajectoryKind::BallisticArc;
    const float frontX          = start.x - candidate.frontPoint.x;
    const float frontZ          = start.z - candidate.frontPoint.z;
    const float contactDistance = profile.capsuleRadius + 2.f * profile.skin;
    const bool  contactVault    = action.kind == ClimbingActionKind::Vault &&
                              frontX * frontX + frontZ * frontZ <= contactDistance * contactDistance;
    if (!ballistic && !surfaceFollow &&
        (action.kind == ClimbingActionKind::Mantle || action.kind == ClimbingActionKind::ClimbUp || contactVault)) {
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
