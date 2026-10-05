#include "graphics/fog/FogRayMarch.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] std::optional<FogRayInterval> intersectSlab(float origin, float dir, float minV,
                                                          float maxV) {
    if (std::fabs(dir) < 1e-8f) {
        if (origin < minV || origin > maxV) return std::nullopt;
        return FogRayInterval{0.f, 1e6f};
    }
    float t0 = (minV - origin) / dir;
    float t1 = (maxV - origin) / dir;
    if (t0 > t1) std::swap(t0, t1);
    return FogRayInterval{t0, t1};
}

[[nodiscard]] std::optional<FogRayInterval> intersectRayAabb(const glm::vec3& o, const glm::vec3& d,
                                                             const glm::vec3& mn, const glm::vec3& mx) {
    auto sx = intersectSlab(o.x, d.x, mn.x, mx.x);
    auto sy = intersectSlab(o.y, d.y, mn.y, mx.y);
    auto sz = intersectSlab(o.z, d.z, mn.z, mx.z);
    if (!sx || !sy || !sz) return std::nullopt;
    const float tEnter = std::max({sx->tEnter, sy->tEnter, sz->tEnter, 0.f});
    const float tExit = std::min({sx->tExit, sy->tExit, sz->tExit});
    if (tExit <= tEnter) return std::nullopt;
    return FogRayInterval{tEnter, tExit};
}

}  // namespace

std::optional<FogRayInterval> FogRayMarch::intersectBound(const glm::vec3& origin,
                                                          const glm::vec3& direction,
                                                          const FogVolumeBound& bound) {
    const float len = glm::length(direction);
    if (!(len > 1e-8f)) return std::nullopt;
    const glm::vec3 d = direction / len;

    switch (bound.kind) {
        case FogVolumeBound::Kind::SkyShell: {
            // Ray vs sphere centered at origin xz / y=0 shell approximation.
            const glm::vec3 center(origin.x, 0.f, origin.z);
            const glm::vec3 oc = origin - center;
            const float b = glm::dot(oc, d);
            const float c = glm::dot(oc, oc) - bound.skyRadius * bound.skyRadius;
            const float disc = b * b - c;
            if (disc < 0.f) return std::nullopt;
            const float s = std::sqrt(disc);
            const float t0 = -b - s;
            const float t1 = -b + s;
            const float tEnter = std::max(t0, 0.f);
            if (t1 <= tEnter) return std::nullopt;
            return FogRayInterval{tEnter, t1};
        }
        case FogVolumeBound::Kind::HeightLayer: {
            auto slab = intersectSlab(origin.y, d.y, bound.heightMin, bound.heightMax);
            if (!slab) return std::nullopt;
            const float tEnter = std::max(slab->tEnter, 0.f);
            if (slab->tExit <= tEnter) return std::nullopt;
            // Cap far distance so infinite rays remain bounded.
            return FogRayInterval{tEnter, std::min(slab->tExit, tEnter + 200.f)};
        }
        case FogVolumeBound::Kind::Obb: {
            // Transform ray into OBB local space, then AABB test.
            const glm::vec3 o = origin - bound.obbCenter;
            const glm::vec3 localO(glm::dot(o, bound.obbAxisX), glm::dot(o, bound.obbAxisY),
                                  glm::dot(o, bound.obbAxisZ));
            const glm::vec3 localD(glm::dot(d, bound.obbAxisX), glm::dot(d, bound.obbAxisY),
                                  glm::dot(d, bound.obbAxisZ));
            return intersectRayAabb(localO, localD, -bound.obbHalfExtents, bound.obbHalfExtents);
        }
    }
    return std::nullopt;
}

Result<FogRayResult> FogRayMarch::integrate(
    const FogDensityField& field, const FogProfile& profile, const glm::vec3& origin,
    const glm::vec3& direction, const FogVolumeBound& bound, const glm::vec3& lightDir,
    const glm::vec3& lightColor, float lightIntensity, const glm::vec3& skyAmbient,
    const glm::vec3& groundAmbient, int sampleCount, float sceneDepth, const BeerLightCache* beerCache,
    bool useAnalyticSegments) const {
    if (sampleCount < 1 || sampleCount > 256) {
        return Result<FogRayResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "sampleCount must be in [1,256]", "sampleCount", {},
            "graphics.fog"));
    }
    const float dirLen = glm::length(direction);
    if (!(dirLen > 1e-8f)) {
        return Result<FogRayResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "direction must be non-zero", "direction", {},
            "graphics.fog"));
    }
    const glm::vec3 dir = direction / dirLen;
    auto interval = intersectBound(origin, dir, bound);
    if (!interval) {
        FogRayResult empty;
        return Result<FogRayResult>::success(empty);
    }
    if (sceneDepth > 0.f) interval->tExit = std::min(interval->tExit, sceneDepth);
    if (!interval->valid()) {
        FogRayResult empty;
        return Result<FogRayResult>::success(empty);
    }

    const glm::vec3 L = glm::length(lightDir) > 1e-6f ? glm::normalize(lightDir) : glm::vec3(0.f, 1.f, 0.f);
    const float cosTheta = glm::dot(-dir, L);
    const float phase = profile.phase(cosTheta);

    FogRayResult result;
    float T = 1.f;
    const float segLen = interval->length() / static_cast<float>(sampleCount);
    float prevDensity = -1.f;

    for (int i = 0; i < sampleCount && T > 1e-4f; ++i) {
        const float t = interval->tEnter + (static_cast<float>(i) + 0.5f) * segLen;
        const glm::vec3 p = origin + dir * t;
        const float density = field.sampleDensity(p);
        const float sigmaT = profile.extinctionAt(density);
        const glm::vec3 sigmaS = profile.scatteringAt(density);

        float stepOptical = sigmaT * segLen;
        float stepT;
        if (useAnalyticSegments && prevDensity >= 0.f &&
            std::fabs(density - prevDensity) < 0.02f * std::max(1.f, prevDensity)) {
            // Closed-form Beer segment for locally stable media.
            stepT = std::exp(-stepOptical);
        } else {
            stepT = std::exp(-stepOptical);
        }
        prevDensity = density;

        float visibility = 1.f;
        if (beerCache && beerCache->valid()) {
            visibility = beerCache->sampleTransmittance(field, p);
        }
        const glm::vec3 ambient = profile.ambientRadiance(skyAmbient, groundAmbient, density);
        const glm::vec3 assist = field.sampleLightAssist(p);
        const glm::vec3 Li = lightColor * lightIntensity * visibility * phase + ambient + assist;
        // Front-to-back: in-scatter attenuated by current transmittance.
        const glm::vec3 scatter = sigmaS * Li * ((1.f - stepT) / std::max(sigmaT, 1e-6f));
        result.inScatter += T * scatter;
        T *= stepT;
        result.opticalDepth += stepOptical;
        ++result.samplesUsed;
    }

    result.transmittance = T;
    return Result<FogRayResult>::success(result);
}

}  // namespace eve::graphics::fog
