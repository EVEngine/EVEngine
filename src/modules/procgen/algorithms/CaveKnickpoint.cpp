#include "procgen/algorithms/CaveKnickpoint.h"

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

float length(CaveHydrologyVec3 value) { return std::sqrt(dot(value, value)); }

CaveHydrologyVec3 normalize(CaveHydrologyVec3 value) {
    const float magnitude = length(value);
    return magnitude > 1e-6f ? multiply(value, 1.f / magnitude) : CaveHydrologyVec3{1.f, 0.f, 0.f};
}

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float downwardSlope(const CaveHydrologyPoint& a, const CaveHydrologyPoint& b) {
    const CaveHydrologyVec3 delta = subtract(b.position, a.position);
    return std::max(0.f, -delta.y / std::max(length(delta), 1e-6f));
}

}  // namespace

std::vector<CaveKnickpointSite> createCaveKnickpointSites(const std::vector<CaveHydrologyPoint>& trunk,
                                                          const std::vector<float>&              hydraulicWeights,
                                                          float                                  sedimentLoad) {
    std::vector<CaveKnickpointSite> sites;
    if (trunk.size() < 4 || sedimentLoad <= 0.f) return sites;
    const float toolAvailability = smoothstep(0.025f, 0.28f, sedimentLoad);
    const float coverProtection  = std::clamp(sedimentLoad * sedimentLoad * 0.68f, 0.f, 0.68f);
    const float toolsAndCover    = toolAvailability * (1.f - coverProtection);
    for (size_t i = 1; i + 1 < trunk.size(); ++i) {
        const float incomingSlope = downwardSlope(trunk[i - 1], trunk[i]);
        const float outgoingSlope = downwardSlope(trunk[i], trunk[i + 1]);
        const float slopeBreak    = std::max(0.f, outgoingSlope - incomingSlope);
        const float drop          = std::max(0.f, trunk[i].position.y - trunk[i + 1].position.y);
        const float radius        = 0.5f * (trunk[i].radius + trunk[i + 1].radius);
        if (outgoingSlope < 0.28f || slopeBreak < 0.12f || drop < radius * 0.25f) continue;

        const CaveHydrologyVec3 tangent = normalize(subtract(trunk[i + 1].position, trunk[i].position));
        CaveHydrologyVec3       lateral = normalize({-tangent.z, 0.f, tangent.x});
        if (std::fabs(tangent.y) > 0.94f) lateral = {0.f, 0.f, 1.f};
        const float       hydraulicIntensity = i < hydraulicWeights.size() ? hydraulicWeights[i] : 1.f;
        const float       erosionPotential   = smoothstep(0.12f, 0.65f, slopeBreak) * toolsAndCover *
                                               std::pow(std::clamp(hydraulicIntensity, 0.25f, 1.6f), 1.15f);
        const float       poolScale          = radius * (0.9f + std::min(drop / std::max(radius, 1e-6f), 1.2f) * 0.35f);
        CaveHydrologyVec3 poolCenter         = add(trunk[i + 1].position, multiply(tangent, radius * 0.28f));
        poolCenter.y -= radius * 0.52f;
        CaveHydrologyVec3 headwallCenter = add(trunk[i].position, multiply(tangent, radius * 0.18f));
        headwallCenter.y -= radius * 0.24f;
        sites.push_back({trunk[i].position, poolCenter, headwallCenter, tangent, lateral, poolScale * 1.65f,
                         poolScale * 0.92f, poolScale * 1.05f, slopeBreak, drop, erosionPotential});
    }
    return sites;
}

CaveKnickpointSample sampleCaveKnickpointErosion(CaveHydrologyVec3                      point,
                                                 const std::vector<CaveKnickpointSite>& sites) {
    CaveKnickpointSample result;
    for (size_t i = 0; i < sites.size(); ++i) {
        const CaveKnickpointSite& site      = sites[i];
        const CaveHydrologyVec3   poolDelta = subtract(point, site.poolCenter);
        const float               along     = dot(poolDelta, site.tangent);
        const float               lateral   = dot(poolDelta, site.lateral);
        const float               vertical  = poolDelta.y;
        const float               poolDistance =
            std::sqrt((along * along) / std::max(site.alongRadius * site.alongRadius, 1e-6f) +
                      (lateral * lateral) / std::max(site.lateralRadius * site.lateralRadius, 1e-6f) +
                      (vertical * vertical) / std::max(site.depthRadius * site.depthRadius, 1e-6f));
        const float poolEnvelope     = 1.f - smoothstep(0.1f, 1.f, poolDistance);
        const float floorBias        = 1.f - smoothstep(-0.12f, 0.68f, vertical / std::max(site.depthRadius, 1e-6f));
        const float verticalDrilling = poolEnvelope * floorBias * site.erosionPotential;

        const CaveHydrologyVec3 headDelta    = subtract(point, site.headwallCenter);
        const float             headAlong    = dot(headDelta, site.tangent);
        const float             headLateral  = dot(headDelta, site.lateral);
        const float             headVertical = headDelta.y;
        const float             headDistance =
            std::sqrt((headAlong * headAlong) / std::max(site.alongRadius * site.alongRadius * 0.22f, 1e-6f) +
                      (headLateral * headLateral) / std::max(site.lateralRadius * site.lateralRadius, 1e-6f) +
                      (headVertical * headVertical) / std::max(site.depthRadius * site.depthRadius * 0.42f, 1e-6f));
        const float headEnvelope     = 1.f - smoothstep(0.12f, 1.f, headDistance);
        const float lowerFace        = 1.f - smoothstep(0.05f, 0.75f, headVertical / std::max(site.depthRadius, 1e-6f));
        const float headwallUndercut = headEnvelope * lowerFace * site.erosionPotential * 0.48f;
        const float erosion          = std::max(verticalDrilling, headwallUndercut);
        if (erosion > result.erosion) result = {erosion, verticalDrilling, headwallUndercut, site.slopeBreak, int(i)};
    }
    return result;
}

}  // namespace eve::procgen
