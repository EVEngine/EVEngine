#include "procgen/algorithms/CaveConstrictionScour.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

CaveHydrologyVec3 subtract(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

CaveHydrologyVec3 add(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

CaveHydrologyVec3 multiply(CaveHydrologyVec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float dot(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

CaveHydrologyVec3 normalize(CaveHydrologyVec3 value) {
    const float length = std::sqrt(dot(value, value));
    return length > 1e-6f ? multiply(value, 1.f / length) : CaveHydrologyVec3{1.f, 0.f, 0.f};
}

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

void appendPathSites(const std::vector<CaveHydrologyPoint>& path, const std::vector<float>& weights,
                     std::vector<CaveConstrictionScourSite>& sites) {
    if (path.size() < 5) return;
    for (size_t i = 2; i + 2 < path.size(); ++i) {
        const float shoulderRadius =
            0.25f * (path[i - 2].radius + path[i - 1].radius + path[i + 1].radius + path[i + 2].radius);
        const float constrictionRatio =
            std::clamp((shoulderRadius - path[i].radius) / std::max(shoulderRadius, 1e-6f), 0.f, 1.f);
        if (constrictionRatio < 0.06f || path[i].radius > path[i - 1].radius || path[i].radius > path[i + 1].radius)
            continue;

        const CaveHydrologyVec3 tangent = normalize(subtract(path[i + 1].position, path[i - 1].position));
        CaveHydrologyVec3       lateral = normalize({-tangent.z, 0.f, tangent.x});
        if (std::fabs(tangent.y) > 0.94f) lateral = {0.f, 0.f, 1.f};
        const float hydraulicIntensity  = i < weights.size() ? weights[i] : 1.f;
        const float dischargeState      = std::clamp((hydraulicIntensity - 0.25f) / 1.35f, 0.f, 1.f);
        const float optimalConstriction = 0.35f + 0.15f * dischargeState;
        const float optimalOffset       = (constrictionRatio - optimalConstriction) / 0.16f;
        const float plungingEfficiency =
            smoothstep(0.08f, 0.22f, constrictionRatio) * std::exp(-0.5f * optimalOffset * optimalOffset);
        const float       poolScale = shoulderRadius * (0.9f + 0.55f * constrictionRatio);
        CaveHydrologyVec3 center    = add(path[i].position, multiply(tangent, shoulderRadius * 0.9f));
        center.y -= shoulderRadius * (0.25f + 0.45f * constrictionRatio);
        sites.push_back({path[i].position, center, tangent, lateral, poolScale * 2.4f, poolScale * 1.2f,
                         poolScale * 1.05f, constrictionRatio, hydraulicIntensity, optimalConstriction,
                         plungingEfficiency});
    }
}

}  // namespace

std::vector<CaveConstrictionScourSite> createCaveConstrictionScourSites(
    const std::vector<CaveHydrologyPoint>& trunk, const std::vector<CaveHydrologyBranch>& branches,
    const CaveHydrologyWeights& hydrology) {
    std::vector<CaveConstrictionScourSite> sites;
    appendPathSites(trunk, hydrology.trunk, sites);
    for (size_t i = 0; i < branches.size(); ++i) {
        const std::vector<float> empty;
        appendPathSites(branches[i].points, i < hydrology.branches.size() ? hydrology.branches[i] : empty, sites);
    }
    return sites;
}

CaveConstrictionScourSample sampleCaveConstrictionScour(CaveHydrologyVec3                             point,
                                                        const std::vector<CaveConstrictionScourSite>& sites) {
    CaveConstrictionScourSample result;
    for (size_t i = 0; i < sites.size(); ++i) {
        const CaveConstrictionScourSite& site     = sites[i];
        const CaveHydrologyVec3          delta    = subtract(point, site.poolCenter);
        const float                      along    = dot(delta, site.tangent);
        const float                      lateral  = dot(delta, site.lateral);
        const float                      vertical = delta.y;
        const float                      normalizedDistance =
            std::sqrt((along * along) / std::max(site.alongRadius * site.alongRadius, 1e-6f) +
                      (lateral * lateral) / std::max(site.lateralRadius * site.lateralRadius, 1e-6f) +
                      (vertical * vertical) / std::max(site.depthRadius * site.depthRadius, 1e-6f));
        const float envelope     = 1.f - smoothstep(0.12f, 1.f, normalizedDistance);
        const float flow         = std::clamp(site.hydraulicIntensity, 0.25f, 1.6f);
        const float bedBias      = 1.f - smoothstep(-0.05f, 0.7f, vertical / std::max(site.depthRadius, 1e-6f));
        const float exitPosition = along / std::max(site.alongRadius, 1e-6f);
        const float poolEntranceStress =
            0.55f + 0.45f * std::exp(-0.5f * std::pow((exitPosition + 0.35f) / 0.32f, 2.f));
        const float exitBias = smoothstep(0.05f, 0.65f, exitPosition) *
                               (1.f - smoothstep(0.65f, 1.f, std::fabs(lateral) / std::max(site.lateralRadius, 1e-6f)));
        const float bedScour = envelope * bedBias * site.plungingEfficiency * poolEntranceStress * flow;
        const float exitWidening = envelope * exitBias * site.plungingEfficiency * flow * 0.82f;
        const float erosion      = std::max(bedScour, exitWidening);
        if (erosion > result.erosion) {
            result = {erosion, bedScour, exitWidening, site.constrictionRatio, site.plungingEfficiency, int(i)};
        }
    }
    return result;
}

}  // namespace eve::procgen
