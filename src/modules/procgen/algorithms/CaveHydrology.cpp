#include "procgen/algorithms/CaveHydrology.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float segmentCapacity(const CaveHydrologyPoint& a, const CaveHydrologyPoint& b, float gradient) {
    const float dx            = b.position.x - a.position.x;
    const float dy            = b.position.y - a.position.y;
    const float dz            = b.position.z - a.position.z;
    const float length        = std::max(1e-5f, std::sqrt(dx * dx + dy * dy + dz * dz));
    const float gravitySlope  = std::max(0.f, -dy / length);
    const float localGradient = gradient * (0.72f + 0.28f * gravitySlope);
    const float radius        = 0.5f * (a.radius + b.radius);
    const float velocity      = std::pow(radius, 2.f / 3.f) * std::sqrt(localGradient);
    return velocity * radius * radius;
}

}  // namespace

CaveHydrologyWeights buildCaveHydrology(const std::vector<CaveHydrologyPoint>&  trunk,
                                        const std::vector<CaveHydrologyBranch>& branches, float erosion, float gradient,
                                        float recharge, float focusing, float damkohler, float transportG) {
    CaveHydrologyWeights result;
    result.trunk.assign(trunk.size() > 1 ? trunk.size() - 1 : 0, 1.f);
    result.branches.resize(branches.size());
    for (size_t branch = 0; branch < branches.size(); ++branch)
        result.branches[branch].assign(branches[branch].points.size() > 1 ? branches[branch].points.size() - 1 : 0,
                                       1.f);
    if (erosion <= 0.f || trunk.size() < 2) return result;

    if (damkohler < 0.0008f)
        result.dissolutionRegime = "uniform";
    else if (damkohler < 0.008f)
        result.dissolutionRegime = "channeling";
    else
        result.dissolutionRegime = "wormholing";
    // Da^-1 is the reactant penetration length relative to the network length.
    // G raises it when transverse diffusion is too slow to consume reactant near the inlet.
    result.reactantPenetration = std::clamp((0.002f / damkohler) * std::sqrt(transportG), 0.08f, 8.f);

    std::vector<float>              branchInflows(trunk.size(), 0.f);
    std::vector<std::vector<float>> rawBranches(branches.size());
    float                           maximumRaw = 1e-6f;
    for (size_t branch = 0; branch < branches.size(); ++branch) {
        const auto& path = branches[branch].points;
        rawBranches[branch].resize(path.size() > 1 ? path.size() - 1 : 0);
        float inletFlow = 0.f;
        for (size_t segment = 0; segment + 1 < path.size(); ++segment) {
            // Distributed recharge enters the remote branch and accumulates toward its
            // junction at segment zero. This preserves small abandoned distal passages.
            const float towardJunction   = float(path.size() - 1 - segment) / float(path.size() - 1);
            const float flow             = segmentCapacity(path[segment], path[segment + 1], gradient) * recharge *
                                           (0.25f + 0.75f * towardJunction);
            rawBranches[branch][segment] = flow;
            inletFlow                    = std::max(inletFlow, flow);
            maximumRaw                   = std::max(maximumRaw, flow);
        }
        const size_t anchor = size_t(std::clamp(branches[branch].trunkAnchor, 0, int(trunk.size() - 1)));
        branchInflows[anchor] += inletFlow;
    }

    std::vector<float> rawTrunk(result.trunk.size(), 0.f);
    float              accumulatedBranches = 0.f;
    for (size_t segment = 0; segment + 1 < trunk.size(); ++segment) {
        accumulatedBranches += branchInflows[segment];
        const float distributedRecharge = recharge * (0.35f + 0.65f * float(segment + 1) / float(trunk.size() - 1));
        rawTrunk[segment] =
            segmentCapacity(trunk[segment], trunk[segment + 1], gradient) * distributedRecharge + accumulatedBranches;
        maximumRaw = std::max(maximumRaw, rawTrunk[segment]);
    }

    const float transportCapacity = std::clamp(std::sqrt(gradient / 0.35f) * (0.35f + recharge), 0.25f, 1.45f);
    const float diffusionLimit    = std::clamp(std::log2(transportG + 1.f) / 2.585f, 0.f, 1.f);
    float       channelExponent   = 0.55f;
    if (result.dissolutionRegime == "channeling")
        channelExponent = 1.55f - 0.45f * diffusionLimit;
    else if (result.dissolutionRegime == "wormholing")
        channelExponent = 2.8f - 1.2f * diffusionLimit;
    auto convert = [&](float raw, float distanceFromInlet) {
        const float normalized   = raw / maximumRaw;
        const float regimeFlow   = std::pow(std::max(normalized, 1e-6f), channelExponent);
        const float focused      = (1.f - focusing) + focusing * regimeFlow;
        const float penetration  = result.dissolutionRegime == "uniform"
                                       ? 1.f
                                       : 0.35f + 0.65f * std::exp(-distanceFromInlet / result.reactantPenetration);
        const float reactiveFlow = std::clamp(focused * penetration * transportCapacity, 0.25f, 1.45f);
        return 1.f + erosion * (reactiveFlow - 1.f);
    };
    result.minimum = 1.45f;
    result.maximum = 0.25f;
    for (size_t segment = 0; segment < rawTrunk.size(); ++segment) {
        const float distance  = rawTrunk.size() > 1 ? float(segment) / float(rawTrunk.size() - 1) : 0.f;
        result.trunk[segment] = convert(rawTrunk[segment], distance);
        result.minimum        = std::min(result.minimum, result.trunk[segment]);
        result.maximum        = std::max(result.maximum, result.trunk[segment]);
    }
    for (size_t branch = 0; branch < rawBranches.size(); ++branch) {
        for (size_t segment = 0; segment < rawBranches[branch].size(); ++segment) {
            const float distance = rawBranches[branch].size() > 1 ? float(rawBranches[branch].size() - 1 - segment) /
                                                                        float(rawBranches[branch].size() - 1)
                                                                  : 0.f;
            result.branches[branch][segment] = convert(rawBranches[branch][segment], distance);
            result.minimum                   = std::min(result.minimum, result.branches[branch][segment]);
            result.maximum                   = std::max(result.maximum, result.branches[branch][segment]);
        }
    }
    return result;
}

}  // namespace eve::procgen
