#include "graphics/fog/AnalyticalVolLight.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {

std::optional<FogRayInterval> AnalyticalVolLight::intersectBeam(const glm::vec3& origin,
                                                                const glm::vec3& direction,
                                                                const AnalyticBeam& beam) {
    const float dirLen = glm::length(direction);
    const float beamLen = glm::length(beam.direction);
    if (!(dirLen > 1e-8f) || !(beamLen > 1e-8f)) return std::nullopt;
    if (!(beam.farDistance > beam.nearDistance) || beam.nearRadius < 0.f || beam.farRadius < 0.f)
        return std::nullopt;

    const glm::vec3 d = direction / dirLen;
    const glm::vec3 axis = beam.direction / beamLen;

    // Project the ray onto the beam axis and keep the capped interval, then
    // reject samples that leave the linearly interpolated radius.
    float tEnter = 1e9f;
    float tExit = -1e9f;
    constexpr int kProbe = 32;
    for (int i = 0; i <= kProbe; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kProbe);
        const float along = beam.nearDistance + u * (beam.farDistance - beam.nearDistance);
        const glm::vec3 center = beam.apex + axis * along;
        const float radius =
            beam.nearRadius + u * (beam.farRadius - beam.nearRadius);
        // Solve |o + t d - center|^2 = radius^2 for the nearest positive hit.
        const glm::vec3 oc = origin - center;
        const float b = glm::dot(oc, d);
        const float c = glm::dot(oc, oc) - radius * radius;
        const float disc = b * b - c;
        if (disc < 0.f) continue;
        const float s = std::sqrt(disc);
        for (float t : {-b - s, -b + s}) {
            if (t < 0.f) continue;
            const glm::vec3 p = origin + d * t;
            const float axial = glm::dot(p - beam.apex, axis);
            if (axial < beam.nearDistance || axial > beam.farDistance) continue;
            tEnter = std::min(tEnter, t);
            tExit = std::max(tExit, t);
        }
        // Also accept the sample if the ray point at axial distance is inside.
        const float tAxis = glm::dot(center - origin, d);
        if (tAxis > 0.f) {
            const glm::vec3 p = origin + d * tAxis;
            if (glm::length(p - center) <= radius) {
                tEnter = std::min(tEnter, tAxis);
                tExit = std::max(tExit, tAxis);
            }
        }
    }
    if (tExit <= tEnter || tEnter > 1e8f) return std::nullopt;
    return FogRayInterval{tEnter, tExit};
}

Result<FogRayResult> AnalyticalVolLight::integrate(const AnalyticBeam& beam, const FogProfile& profile,
                                                   const glm::vec3& origin, const glm::vec3& direction,
                                                   float sceneDepth, int segments,
                                                   float mediumDensity) const {
    if (segments != 2 && segments != 4 && segments != 8) {
        return Result<FogRayResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "segments must be 2, 4, or 8", "segments", {},
            "graphics.fog"));
    }
    if (!std::isfinite(mediumDensity) || mediumDensity < 0.f) {
        return Result<FogRayResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "mediumDensity must be finite and >= 0",
            "mediumDensity", {}, "graphics.fog"));
    }

    auto interval = intersectBeam(origin, direction, beam);
    FogRayResult result;
    if (!interval) return Result<FogRayResult>::success(result);
    if (sceneDepth > 0.f) interval->tExit = std::min(interval->tExit, sceneDepth);
    if (!interval->valid()) return Result<FogRayResult>::success(result);

    const glm::vec3 dir = glm::normalize(direction);
    const glm::vec3 axis = glm::normalize(beam.direction);
    const float sigmaT = profile.extinctionAt(mediumDensity);
    const glm::vec3 sigmaS = profile.scatteringAt(mediumDensity);
    const float phase = profile.phase(glm::dot(-dir, -axis));

    float T = 1.f;
    const float seg = interval->length() / static_cast<float>(segments);
    for (int i = 0; i < segments && T > 1e-4f; ++i) {
        const float t = interval->tEnter + (static_cast<float>(i) + 0.5f) * seg;
        const glm::vec3 p = origin + dir * t;
        const float axial = glm::dot(p - beam.apex, axis);
        const float u = std::clamp((axial - beam.nearDistance) /
                                       std::max(beam.farDistance - beam.nearDistance, 1e-4f),
                                   0.f, 1.f);
        const float radius = beam.nearRadius + u * (beam.farRadius - beam.nearRadius);
        const glm::vec3 center = beam.apex + axis * axial;
        const float radial = glm::length(p - center) / std::max(radius, 1e-4f);
        const float mask = std::clamp(1.f - radial * radial, 0.f, 1.f);
        const float stepT = std::exp(-sigmaT * seg);
        const glm::vec3 Li = beam.color * beam.intensity * mask * phase;
        result.inScatter += T * sigmaS * Li * ((1.f - stepT) / std::max(sigmaT, 1e-6f));
        T *= stepT;
        result.opticalDepth += sigmaT * seg;
        ++result.samplesUsed;
    }
    result.transmittance = T;
    return Result<FogRayResult>::success(result);
}

}  // namespace eve::graphics::fog
