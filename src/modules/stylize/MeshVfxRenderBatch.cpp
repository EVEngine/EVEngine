#include "MeshVfxRenderBatch.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include "MeshVfxScalability.h"

// MeshVfxRenderBatch.cpp
namespace eve::stylize {
namespace {

struct PlannedDraw {
    MeshVfxBatchKey    key;
    float              viewDepth = 0.0f;
    MeshVfxBatchedDraw draw;
};

bool keyLess(const MeshVfxBatchKey& lhs, const MeshVfxBatchKey& rhs) { return lhs < rhs; }

void appendBatch(std::vector<MeshVfxRenderBatch>& batches, PlannedDraw&& planned) {
    if (batches.empty() || batches.back().key != planned.key) {
        batches.push_back(MeshVfxRenderBatch{planned.key, {}});
    }
    batches.back().draws.push_back(planned.draw);
}

}  // namespace

MeshVfxRenderQueue MeshVfxRenderBatchPlanner::build(std::span<const MeshVfxRenderItem>  items,
                                                    std::span<const MeshVfxLodDecision> lodDecisions,
                                                    std::uint64_t                       frameIndex) const {
    MeshVfxRenderQueue queue;
    queue.stats.inputItems = static_cast<std::uint32_t>(items.size());

    std::unordered_map<std::uint64_t, const MeshVfxLodDecision*> decisions;
    decisions.reserve(lodDecisions.size());
    for (const auto& decision : lodDecisions) {
        decisions.try_emplace(decision.stableInstanceId, &decision);
    }

    std::vector<PlannedDraw> additive;
    std::vector<PlannedDraw> alpha;
    additive.reserve(items.size());
    alpha.reserve(items.size());
    for (const auto& item : items) {
        const auto found = decisions.find(item.stableInstanceId);
        if (found == decisions.end()) {
            ++queue.stats.missingDecisions;
            continue;
        }
        const auto& lod = *found->second;
        if (lod.tier == MeshVfxLodTier::Culled) {
            ++queue.stats.culledDraws;
            continue;
        }

        const auto  interval = std::max(1u, lod.meshUpdateInterval);
        const auto  refresh  = frameIndex % interval == item.stableInstanceId % interval;
        PlannedDraw planned{
            item.key,
            std::isfinite(item.viewDepth) ? item.viewDepth : 0.0f,
            {item.stableInstanceId, lod.tier, refresh ? MeshVfxMeshUpdate::Refresh : MeshVfxMeshUpdate::Reuse,
             std::max(1u, lod.trailSampleStride)}};
        if (refresh) {
            ++queue.stats.dynamicMeshRefreshes;
        }
        (item.key.blend == MeshVfxBatchBlend::Additive ? additive : alpha).push_back(std::move(planned));
    }

    std::sort(additive.begin(), additive.end(), [](const PlannedDraw& lhs, const PlannedDraw& rhs) {
        if (lhs.key != rhs.key) {
            return keyLess(lhs.key, rhs.key);
        }
        return lhs.draw.stableInstanceId < rhs.draw.stableInstanceId;
    });
    std::sort(alpha.begin(), alpha.end(), [](const PlannedDraw& lhs, const PlannedDraw& rhs) {
        if (lhs.viewDepth != rhs.viewDepth) {
            return lhs.viewDepth > rhs.viewDepth;
        }
        if (lhs.key != rhs.key) {
            return keyLess(lhs.key, rhs.key);
        }
        return lhs.draw.stableInstanceId < rhs.draw.stableInstanceId;
    });

    queue.batches.reserve(additive.size() + alpha.size());
    for (auto& planned : additive) {
        appendBatch(queue.batches, std::move(planned));
        ++queue.stats.queuedDraws;
    }
    for (auto& planned : alpha) {
        appendBatch(queue.batches, std::move(planned));
        ++queue.stats.queuedDraws;
    }
    return queue;
}

}  // namespace eve::stylize

// MeshVfxScalability.cpp
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
    decision.tier             = tier;
    switch (tier) {
        case MeshVfxLodTier::Full: decision.workUnits = policy.fullWorkUnits; break;
        case MeshVfxLodTier::Reduced:
            decision.workUnits          = policy.reducedWorkUnits;
            decision.trailSampleStride  = std::max(1u, policy.reducedTrailSampleStride);
            decision.meshUpdateInterval = std::max(1u, policy.reducedMeshUpdateInterval);
            break;
        case MeshVfxLodTier::Minimal:
            decision.workUnits          = policy.minimalWorkUnits;
            decision.trailSampleStride  = std::max(1u, policy.minimalTrailSampleStride);
            decision.meshUpdateInterval = std::max(1u, policy.minimalMeshUpdateInterval);
            break;
        case MeshVfxLodTier::Culled: break;
    }
    return decision;
}

MeshVfxLodTier degrade(MeshVfxLodTier tier) {
    switch (tier) {
        case MeshVfxLodTier::Full: return MeshVfxLodTier::Reduced;
        case MeshVfxLodTier::Reduced: return MeshVfxLodTier::Minimal;
        case MeshVfxLodTier::Minimal:
        case MeshVfxLodTier::Culled: return MeshVfxLodTier::Culled;
    }
    return MeshVfxLodTier::Culled;
}

}  // namespace

MeshVfxScalabilityPlanner::MeshVfxScalabilityPlanner(MeshVfxScalabilityPolicy policy) : policy_(policy) {}

std::vector<MeshVfxLodDecision> MeshVfxScalabilityPlanner::plan(std::span<const MeshVfxLodCandidate> candidates) const {
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
        auto tier     = requestedTier(candidates[index], policy_);
        auto decision = makeDecision(candidates[index].stableInstanceId, tier, policy_);
        while (decision.workUnits > remaining && tier != MeshVfxLodTier::Culled) {
            tier     = degrade(tier);
            decision = makeDecision(candidates[index].stableInstanceId, tier, policy_);
        }
        remaining -= decision.workUnits;
        decisions[index] = decision;
    }
    return decisions;
}

}  // namespace eve::stylize
