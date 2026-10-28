#include "procgen/algorithms/CaveMixingCorrosion.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

CaveHydrologyVec3 subtract(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

float dot(CaveHydrologyVec3 a, CaveHydrologyVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

CaveHydrologyVec3 normalize(CaveHydrologyVec3 value) {
    const float length = std::sqrt(dot(value, value));
    return length > 1e-6f ? CaveHydrologyVec3{value.x / length, value.y / length, value.z / length}
                          : CaveHydrologyVec3{1.f, 0.f, 0.f};
}

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

uint32_t hash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

}  // namespace

std::vector<CaveMixingSite> createCaveMixingSites(const std::vector<CaveHydrologyPoint>&  trunk,
                                                  const std::vector<CaveHydrologyBranch>& branches, uint32_t seed) {
    std::vector<CaveMixingSite> sites;
    sites.reserve(branches.size());
    for (size_t branchIndex = 0; branchIndex < branches.size(); ++branchIndex) {
        const CaveHydrologyBranch& branch = branches[branchIndex];
        if (trunk.size() < 2 || branch.points.size() < 2) continue;
        const size_t            anchor         = size_t(std::clamp(branch.trunkAnchor, 0, int(trunk.size() - 1)));
        const size_t            before         = anchor > 0 ? anchor - 1 : anchor;
        const size_t            after          = std::min(anchor + 1, trunk.size() - 1);
        const CaveHydrologyVec3 tangent        = normalize(subtract(trunk[after].position, trunk[before].position));
        const float             trunkRadius    = trunk[anchor].radius;
        const float             branchRadius   = branch.points[1].radius;
        const float             trunkCapacity  = trunkRadius * trunkRadius;
        const float             branchCapacity = branchRadius * branchRadius;
        const float             branchFraction = branchCapacity / std::max(trunkCapacity + branchCapacity, 1e-6f);
        const float             balancedMixing = 4.f * branchFraction * (1.f - branchFraction);
        const float             chemistryContrast =
            0.45f + 0.55f * (float(hash(seed ^ uint32_t(branchIndex) * 0x9e3779b9u) & 0xffffu) / 65535.f);
        const float radius = 0.5f * (trunkRadius + branchRadius);
        sites.push_back(
            {trunk[anchor].position, tangent, radius * 2.1f, radius * 1.18f, balancedMixing * chemistryContrast});
    }
    return sites;
}

CaveMixingCorrosionSample sampleCaveMixingCorrosion(CaveHydrologyVec3 point, const std::vector<CaveMixingSite>& sites) {
    CaveMixingCorrosionSample result;
    for (size_t index = 0; index < sites.size(); ++index) {
        const CaveMixingSite&   site  = sites[index];
        const CaveHydrologyVec3 delta = subtract(point, site.center);
        const float             along = dot(delta, site.tangent);
        const CaveHydrologyVec3 radial{delta.x - site.tangent.x * along, delta.y - site.tangent.y * along,
                                       delta.z - site.tangent.z * along};
        const float             normalizedDistance =
            std::sqrt((along * along) / std::max(site.alongRadius * site.alongRadius, 1e-6f) +
                      dot(radial, radial) / std::max(site.radialRadius * site.radialRadius, 1e-6f));
        const float envelope = 1.f - smoothstep(0.18f, 1.f, normalizedDistance);
        const float erosion  = envelope * site.mixingPotential;
        if (erosion > result.erosion) {
            result.erosion         = erosion;
            result.mixingPotential = site.mixingPotential;
            result.siteIndex       = int(index);
        }
    }
    return result;
}

}  // namespace eve::procgen
