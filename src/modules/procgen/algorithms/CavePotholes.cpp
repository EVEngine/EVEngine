#include "procgen/algorithms/CavePotholes.h"

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

float ellipsoid(CaveHydrologyVec3 point, const CavePotholeSite& site, CaveHydrologyVec3 center, float scale) {
    const CaveHydrologyVec3 delta    = subtract(point, center);
    const float             along    = dot(delta, site.tangent) / std::max(site.alongRadius * scale, 1e-6f);
    const float             lateral  = dot(delta, site.lateral) / std::max(site.lateralRadius * scale, 1e-6f);
    const float             vertical = delta.y / std::max(site.depthRadius * scale, 1e-6f);
    return 1.f - smoothstep(0.12f, 1.f, std::sqrt(along * along + lateral * lateral + vertical * vertical));
}

}  // namespace

std::vector<CavePotholeSite> createCavePotholeSites(const std::vector<CaveHydrologyPoint>& trunk,
                                                    const std::vector<float>&              hydraulicWeights,
                                                    const std::vector<CaveFracture>&       fractures,
                                                    float apertureVariability, float stressControl, float sedimentLoad,
                                                    float gravelSize, uint32_t seed) {
    std::vector<CavePotholeSite> sites;
    if (trunk.size() < 2 || fractures.size() < 2 || sedimentLoad <= 0.f) return sites;
    const float tools = smoothstep(0.025f, 0.28f, sedimentLoad);
    const float cover = std::clamp(sedimentLoad * sedimentLoad * 0.68f, 0.f, 0.68f);
    for (size_t segment = 0; segment + 1 < trunk.size(); ++segment) {
        const CaveHydrologyVec3 delta   = subtract(trunk[segment + 1].position, trunk[segment].position);
        const CaveHydrologyVec3 tangent = normalize(delta);
        CaveHydrologyVec3       lateral = normalize({-tangent.z, 0.f, tangent.x});
        if (std::fabs(tangent.y) > 0.94f) lateral = {0.f, 0.f, 1.f};
        const float hydraulic = segment < hydraulicWeights.size() ? hydraulicWeights[segment] : 1.f;
        if (hydraulic < 0.32f) continue;
        for (int sampleIndex = 1; sampleIndex <= 8; ++sampleIndex) {
            const float             t         = float(sampleIndex) / 9.f;
            const CaveHydrologyVec3 pathPoint = add(trunk[segment].position, multiply(delta, t));
            float                   strongest = 0.f;
            float                   second    = 0.f;
            for (const CaveFracture& fracture : fractures) {
                const CaveFractureSample fractureSample = sampleCaveFracture(
                    {pathPoint.x, pathPoint.y, pathPoint.z, fracture, apertureVariability, stressControl, seed});
                if (fractureSample.mask > strongest) {
                    second    = strongest;
                    strongest = fractureSample.mask;
                } else {
                    second = std::max(second, fractureSample.mask);
                }
            }
            const float intersection = strongest * second;
            if (intersection < 0.055f) continue;
            const float       radius = trunk[segment].radius + (trunk[segment + 1].radius - trunk[segment].radius) * t;
            CaveHydrologyVec3 center = pathPoint;
            center.y -= radius * 0.88f;
            const float minimumSpacing = radius * 1.45f;
            if (!sites.empty() && length(subtract(center, sites.back().center)) < minimumSpacing) continue;
            const float potential = smoothstep(0.04f, 0.42f, intersection) * tools * (1.f - cover) *
                                    std::pow(std::clamp(hydraulic, 0.25f, 1.6f), 1.2f);
            const float footprint = radius * (0.42f + 0.18f * potential);
            sites.push_back({center, tangent, lateral, footprint * 1.12f, footprint, footprint * 1.28f, intersection,
                             potential, std::clamp(gravelSize, 0.f, 1.f)});
            break;
        }
    }
    return sites;
}

CavePotholeSample sampleCavePotholeErosion(CaveHydrologyVec3 point, const std::vector<CavePotholeSite>& sites) {
    CavePotholeSample result;
    for (size_t i = 0; i < sites.size(); ++i) {
        const CavePotholeSite&  site           = sites[i];
        const float             fineTools      = 1.f - site.gravelSize;
        const float             downstreamBias = fineTools * 2.f - 1.f;
        const CaveHydrologyVec3 primaryCenter =
            add(site.center, multiply(site.tangent, site.alongRadius * downstreamBias * 0.22f));
        const float       spread          = 0.72f + fineTools * 0.38f;
        const float       wallAndBed      = ellipsoid(point, site, primaryCenter, spread) * site.erosionPotential;
        CaveHydrologyVec3 secondaryCenter = site.center;
        secondaryCenter.y -= site.depthRadius * 0.72f;
        const float secondary = ellipsoid(point, site, secondaryCenter, 0.46f) * site.erosionPotential *
                                smoothstep(0.38f, 0.82f, site.erosionPotential) * 0.72f;
        const float erosion   = std::max(wallAndBed, secondary);
        if (erosion > result.erosion) result = {erosion, wallAndBed, secondary, downstreamBias, int(i)};
    }
    return result;
}

}  // namespace eve::procgen
