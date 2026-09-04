#include "MeshVfxRenderBatch.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace eve::stylize {
namespace {

struct PlannedDraw {
    MeshVfxBatchKey key;
    float viewDepth = 0.0f;
    MeshVfxBatchedDraw draw;
};

bool keyLess(const MeshVfxBatchKey& lhs, const MeshVfxBatchKey& rhs) { return lhs < rhs; }

void appendBatch(std::vector<MeshVfxRenderBatch>& batches, PlannedDraw&& planned) {
    if (batches.empty() || batches.back().key != planned.key) {
        batches.push_back(MeshVfxRenderBatch{planned.key, {}});
    }
    batches.back().draws.push_back(planned.draw);
}

} // namespace

MeshVfxRenderQueue MeshVfxRenderBatchPlanner::build(std::span<const MeshVfxRenderItem> items,
                                                     std::span<const MeshVfxLodDecision> lodDecisions,
                                                     std::uint64_t frameIndex) const {
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

        const auto interval = std::max(1u, lod.meshUpdateInterval);
        const auto refresh = frameIndex % interval == item.stableInstanceId % interval;
        PlannedDraw planned{item.key,
                            std::isfinite(item.viewDepth) ? item.viewDepth : 0.0f,
                            {item.stableInstanceId,
                             lod.tier,
                             refresh ? MeshVfxMeshUpdate::Refresh : MeshVfxMeshUpdate::Reuse,
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

} // namespace eve::stylize
