#include "MeshVfxScalability.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eve::stylize {
namespace {

MeshVfxLodTier requestedTier(const MeshVfxLodCandidate& candidate, const MeshVfxScalabilityPolicy& policy) {
    if (!candidate.visible || !std::isfinite(candidate.distance) || candidate.distance < 0.0f ||
        candidate.distance > policy.maximumDistance || !std::isfinite(candidate.projectedRadiusPixels)) {
        return MeshVfxLodTier::Culled;
    }
    if (candidate.projectedRadiusPixels >= policy.fullQualityMinPixels) {
        return MeshVfxLodTier::Full;
    }
    if (candidate.projectedRadiusPixels >= policy.reducedQualityMinPixels) {
        return MeshVfxLodTier::Reduced;
    }
    return MeshVfxLodTier::Minimal;
}

MeshVfxLodDecision makeDecision(std::uint64_t id, MeshVfxLodTier tier, const MeshVfxScalabilityPolicy& policy) {
    MeshVfxLodDecision decision;
    decision.stableInstanceId = id;
    decision.tier = tier;
    switch (tier) {
    case MeshVfxLodTier::Full:
        decision.workUnits = policy.fullWorkUnits;
        break;
    case MeshVfxLodTier::Reduced:
        decision.workUnits = policy.reducedWorkUnits;
        decision.trailSampleStride = std::max(1u, policy.reducedTrailSampleStride);
        decision.meshUpdateInterval = std::max(1u, policy.reducedMeshUpdateInterval);
        break;
    case MeshVfxLodTier::Minimal:
        decision.workUnits = policy.minimalWorkUnits;
        decision.trailSampleStride = std::max(1u, policy.minimalTrailSampleStride);
        decision.meshUpdateInterval = std::max(1u, policy.minimalMeshUpdateInterval);
        break;
    case MeshVfxLodTier::Culled:
        break;
    }
    return decision;
}

MeshVfxLodTier degrade(MeshVfxLodTier tier) {
    switch (tier) {
    case MeshVfxLodTier::Full:
        return MeshVfxLodTier::Reduced;
    case MeshVfxLodTier::Reduced:
        return MeshVfxLodTier::Minimal;
    case MeshVfxLodTier::Minimal:
    case MeshVfxLodTier::Culled:
        return MeshVfxLodTier::Culled;
    }
    return MeshVfxLodTier::Culled;
}

} // namespace

MeshVfxScalabilityPlanner::MeshVfxScalabilityPlanner(MeshVfxScalabilityPolicy policy) : policy_(policy) {}

std::vector<MeshVfxLodDecision>
MeshVfxScalabilityPlanner::plan(std::span<const MeshVfxLodCandidate> candidates) const {
    std::vector<MeshVfxLodDecision> decisions;
    decisions.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        decisions.push_back(makeDecision(candidate.stableInstanceId, MeshVfxLodTier::Culled, policy_));
    }

    std::vector<std::size_t> order(candidates.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        const auto& a = candidates[lhs];
        const auto& b = candidates[rhs];
        if (a.priority != b.priority) {
            return a.priority > b.priority;
        }
        if (a.distance != b.distance) {
            return a.distance < b.distance;
        }
        return a.stableInstanceId < b.stableInstanceId;
    });

    std::uint32_t remaining = policy_.workUnitBudget;
    for (const auto index : order) {
        auto tier = requestedTier(candidates[index], policy_);
        auto decision = makeDecision(candidates[index].stableInstanceId, tier, policy_);
        while (decision.workUnits > remaining && tier != MeshVfxLodTier::Culled) {
            tier = degrade(tier);
            decision = makeDecision(candidates[index].stableInstanceId, tier, policy_);
        }
        remaining -= decision.workUnits;
        decisions[index] = decision;
    }
    return decisions;
}

} // namespace eve::stylize
