#pragma once

#include "MeshVfxScalability.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::stylize {

/** @brief Blend ordering contract used by the mesh VFX batch planner. */
enum class MeshVfxBatchBlend : std::uint8_t {
    Additive,
    Alpha,
};

/** @brief Immutable GPU-state identity used to combine compatible draw items. */
struct MeshVfxBatchKey {
    std::uint64_t pipelineId = 0;
    std::uint64_t materialId = 0;
    std::uint64_t meshId = 0;
    std::uint32_t renderPass = 0;
    MeshVfxBatchBlend blend = MeshVfxBatchBlend::Additive;

    auto operator<=>(const MeshVfxBatchKey&) const = default;
};

/** @brief One visible mesh VFX draw before sorting and batching. */
struct MeshVfxRenderItem {
    std::uint64_t stableInstanceId = 0;
    MeshVfxBatchKey key;
    float viewDepth = 0.0f;
};

/** @brief Whether an instance must refresh dynamic mesh data this frame. */
enum class MeshVfxMeshUpdate : std::uint8_t {
    Reuse,
    Refresh,
};

/** @brief One ordered draw reference inside a render batch. */
struct MeshVfxBatchedDraw {
    std::uint64_t stableInstanceId = 0;
    MeshVfxLodTier tier = MeshVfxLodTier::Culled;
    MeshVfxMeshUpdate meshUpdate = MeshVfxMeshUpdate::Reuse;
    std::uint32_t trailSampleStride = 1;
};

/** @brief Consecutive compatible draws sharing GPU state. */
struct MeshVfxRenderBatch {
    MeshVfxBatchKey key;
    std::vector<MeshVfxBatchedDraw> draws;
};

/** @brief Summary of rejected or accepted queue entries. */
struct MeshVfxBatchStats {
    std::uint32_t inputItems = 0;
    std::uint32_t queuedDraws = 0;
    std::uint32_t culledDraws = 0;
    std::uint32_t missingDecisions = 0;
    std::uint32_t dynamicMeshRefreshes = 0;
};

/** @brief Complete deterministic queue consumed by a graphics backend. */
struct MeshVfxRenderQueue {
    std::vector<MeshVfxRenderBatch> batches;
    MeshVfxBatchStats stats;
};

/**
 * @brief Builds deterministic state batches while preserving alpha depth order.
 *
 * Additive items may be grouped by state because their composition is order
 * independent. Alpha items are sorted back-to-front and only adjacent compatible
 * items are combined. The planner retains no pointers or cross-frame state.
 */
class MeshVfxRenderBatchPlanner {
public:
    /**
     * @brief Builds one frame's render queue.
     * @param items Candidate draws for the current view.
     * @param lodDecisions LOD decisions keyed by stableInstanceId.
     * @param frameIndex Deterministic simulation/render frame index.
     * @return Ordered render batches and diagnostic counters.
     */
    [[nodiscard]] MeshVfxRenderQueue build(std::span<const MeshVfxRenderItem> items,
                                           std::span<const MeshVfxLodDecision> lodDecisions,
                                           std::uint64_t frameIndex) const;
};

} // namespace eve::stylize
