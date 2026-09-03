#include "stylize/MeshVfxRenderBatch.h"

#include <array>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::stylize;

namespace {

MeshVfxLodDecision visible(std::uint64_t id, MeshVfxLodTier tier, std::uint32_t interval = 1) {
    MeshVfxLodDecision decision;
    decision.stableInstanceId = id;
    decision.tier = tier;
    decision.meshUpdateInterval = interval;
    decision.trailSampleStride = interval;
    return decision;
}

MeshVfxBatchKey key(std::uint64_t material, MeshVfxBatchBlend blend) {
    return MeshVfxBatchKey{1, material, 3, 0, blend};
}

} // namespace

TEST_CASE("stylize.mesh_vfx_batch filters LOD and groups additive state") {
    const std::array items{
        MeshVfxRenderItem{3, key(8, MeshVfxBatchBlend::Additive), 2.0f},
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 4.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 8.0f},
        MeshVfxRenderItem{4, key(9, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Reduced),
        visible(3, MeshVfxLodTier::Minimal),
        visible(4, MeshVfxLodTier::Culled),
    };

    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.stats.culledDraws, 1u);
    REQUIRE_EQ(queue.batches.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 1u);
    REQUIRE_EQ(queue.batches[0].draws[1].stableInstanceId, 2u);
}

TEST_CASE("stylize.mesh_vfx_batch preserves alpha back-to-front order") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Alpha), 2.0f},
        MeshVfxRenderItem{2, key(8, MeshVfxBatchBlend::Alpha), 9.0f},
        MeshVfxRenderItem{3, key(7, MeshVfxBatchBlend::Alpha), 5.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Full),
        visible(3, MeshVfxLodTier::Full),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.batches.size(), 3u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 2u);
    REQUIRE_EQ(queue.batches[1].draws[0].stableInstanceId, 3u);
    REQUIRE_EQ(queue.batches[2].draws[0].stableInstanceId, 1u);
}

TEST_CASE("stylize.mesh_vfx_batch staggers reduced mesh refreshes") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{9, key(7, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Reduced, 2),
        visible(2, MeshVfxLodTier::Reduced, 2),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 4);
    REQUIRE_EQ(queue.stats.queuedDraws, 2u);
    REQUIRE_EQ(queue.stats.missingDecisions, 1u);
    REQUIRE_EQ(queue.stats.dynamicMeshRefreshes, 1u);
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[0].meshUpdate),
               static_cast<int>(MeshVfxMeshUpdate::Reuse));
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[1].meshUpdate),
               static_cast<int>(MeshVfxMeshUpdate::Refresh));
}
