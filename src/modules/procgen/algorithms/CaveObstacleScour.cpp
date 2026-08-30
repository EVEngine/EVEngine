#include "procgen/algorithms/CaveObstacleScour.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

CaveHydrologyVec3 add(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

CaveHydrologyVec3 subtract(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

CaveHydrologyVec3 multiply(CaveHydrologyVec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float dot(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(CaveHydrologyVec3 value) { return std::sqrt(dot(value, value)); }

CaveHydrologyVec3 normalize(CaveHydrologyVec3 value) {
    const float magnitude = length(value);
    return magnitude > 1e-6f ? multiply(value, 1.f / magnitude) : CaveHydrologyVec3{1.f, 0.f, 0.f};
}

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float ellipsoid(CaveHydrologyVec3 point, CaveHydrologyVec3 center, const CaveObstacleScourSite& site, float alongRadius,
                float lateralRadius, float depthRadius) {
    const CaveHydrologyVec3 delta    = subtract(point, center);
    const float             along    = dot(delta, site.tangent) / std::max(alongRadius, 1e-6f);
    const float             lateral  = dot(delta, site.lateral) / std::max(lateralRadius, 1e-6f);
    const float             vertical = delta.y / std::max(depthRadius, 1e-6f);
    return 1.f - smoothstep(0.12f, 1.f, std::sqrt(along * along + lateral * lateral + vertical * vertical));
}

}  // namespace

std::vector<CaveObstacleScourSite> createCaveObstacleScourSites(const CaveBreakdownSet&                breakdown,
                                                                const std::vector<CaveHydrologyPoint>& trunk,
                                                                const std::vector<float>&              hydraulicWeights,
                                                                float sedimentLoad, float wallRoughness) {
    std::vector<CaveObstacleScourSite> sites;
    if (breakdown.events.empty() || trunk.size() < 2 || sedimentLoad <= 0.f) return sites;
    const float tools              = smoothstep(0.025f, 0.28f, sedimentLoad);
    const float cover              = std::clamp(sedimentLoad * sedimentLoad * 0.68f, 0.f, 0.68f);
    const float roughnessRetention = 1.f - 0.72f * smoothstep(0.12f, 0.82f, wallRoughness);
    for (const CaveBreakdownEvent& event : breakdown.events) {
        for (const CaveBreakdownBlock& block : event.blocks) {
            const CaveHydrologyVec3 blockCenter{block.x, block.y, block.z};
            size_t                  nearestSegment  = 0;
            float                   nearestDistance = 1e9f;
            CaveHydrologyVec3       nearestPoint;
            for (size_t segment = 0; segment + 1 < trunk.size(); ++segment) {
                const CaveHydrologyVec3 pathDelta = subtract(trunk[segment + 1].position, trunk[segment].position);
                const float             length2   = dot(pathDelta, pathDelta);
                const float             t =
                    length2 > 1e-8f
                        ? std::clamp(dot(subtract(blockCenter, trunk[segment].position), pathDelta) / length2, 0.f, 1.f)
                        : 0.f;
                const CaveHydrologyVec3 closest  = add(trunk[segment].position, multiply(pathDelta, t));
                const float             distance = length(subtract(blockCenter, closest));
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestSegment  = segment;
                    nearestPoint    = closest;
                }
            }
            const float passageRadius = 0.5f * (trunk[nearestSegment].radius + trunk[nearestSegment + 1].radius);
            if (nearestDistance > passageRadius * 1.65f) continue;
            const CaveHydrologyVec3 tangent =
                normalize(subtract(trunk[nearestSegment + 1].position, trunk[nearestSegment].position));
            CaveHydrologyVec3 lateral = normalize({-tangent.z, 0.f, tangent.x});
            if (std::fabs(tangent.y) > 0.94f) lateral = {0.f, 0.f, 1.f};
            const float hydraulic  = nearestSegment < hydraulicWeights.size() ? hydraulicWeights[nearestSegment] : 1.f;
            const float protrusion = 2.f * block.hy / std::max(passageRadius, 1e-6f);
            const float potential  = tools * (1.f - cover) * roughnessRetention * smoothstep(0.08f, 0.48f, protrusion) *
                                     std::pow(std::clamp(hydraulic, 0.25f, 1.6f), 1.2f);
            if (potential <= 0.01f) continue;
            const float       obstacleWidth = std::max(block.hx, block.hz);
            CaveHydrologyVec3 frontCenter   = add(blockCenter, multiply(tangent, -obstacleWidth * 0.85f));
            CaveHydrologyVec3 wakeCenter    = add(blockCenter, multiply(tangent, obstacleWidth * 1.4f));
            frontCenter.y                   = nearestPoint.y - passageRadius * 0.88f;
            wakeCenter.y                    = frontCenter.y + obstacleWidth * 0.12f;
            sites.push_back({blockCenter, frontCenter, wakeCenter, tangent, lateral, obstacleWidth * 1.15f,
                             obstacleWidth * 1.45f, obstacleWidth * 0.82f, obstacleWidth * 2.35f, potential,
                             roughnessRetention});
        }
    }
    return sites;
}

CaveObstacleScourSample sampleCaveObstacleScour(CaveHydrologyVec3                         point,
                                                const std::vector<CaveObstacleScourSite>& sites) {
    CaveObstacleScourSample result;
    for (size_t i = 0; i < sites.size(); ++i) {
        const CaveObstacleScourSite& site = sites[i];
        const CaveHydrologyVec3 left  = add(site.frontCenter, multiply(site.lateral, site.frontLateralRadius * 0.42f));
        const CaveHydrologyVec3 right = add(site.frontCenter, multiply(site.lateral, -site.frontLateralRadius * 0.42f));
        const float             leftLobe =
            ellipsoid(point, left, site, site.frontAlongRadius, site.frontLateralRadius * 0.68f, site.depthRadius);
        const float rightLobe =
            ellipsoid(point, right, site, site.frontAlongRadius, site.frontLateralRadius * 0.68f, site.depthRadius);
        const float horseshoe = std::max(leftLobe, rightLobe) * site.erosionPotential;
        const float wake      = ellipsoid(point, site.wakeCenter, site, site.wakeAlongRadius,
                                          site.frontLateralRadius * 0.72f, site.depthRadius * 0.52f) *
                                site.erosionPotential * 0.58f;
        const float erosion   = std::max(horseshoe, wake);
        if (erosion > result.erosion) result = {erosion, horseshoe, wake, site.roughnessRetention, int(i)};
    }
    return result;
}

}  // namespace eve::procgen
