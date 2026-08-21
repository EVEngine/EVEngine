#pragma once

#include "graphics/GpuDrivenTypes.h"

#include <cstdint>
#include <vector>
#include <algorithm>

namespace eve::graphics {

/**
 * @brief CPU-side builder for GPU-driven indirect draws (stage 1).
 *
 * Collects (instance, mesh, material, pipeline) tuples, sorts them by a
 * 64-bit key (pipeline > material > mesh, stable by instance sequence), then
 * merges consecutive equal buckets into one GpuIndirectCommand per bucket.
 *
 * The caller writes the instance buffer in `sortedInstanceOrder()` so every
 * bucket is a contiguous range; each command's firstInstance points at the
 * bucket start in that buffer.
 */
class IndirectBuilder {
public:
    void reset();

    /** @brief seq = monotonic instance sequence (stable order tiebreak). */
    void add(uint32_t seq, uint32_t meshId, uint32_t materialId, uint32_t pipelineId);

    /**
     * @brief Sort + merge. `meshTable` supplies indexCount/firstIndex/vertexBase.
     * @return number of indirect draws produced.
     */
    uint32_t build(const std::vector<GpuMeshRecord> &meshTable);

    /** @brief Instance indices in the sorted order the caller must upload. */
    const std::vector<uint32_t> &sortedInstanceOrder() const { return order_; }

    const std::vector<GpuIndirectCommand> &commands() const { return commands_; }
    uint32_t drawCount() const { return uint32_t(commands_.size()); }

private:
    struct Entry {
        uint64_t key = 0;
        uint32_t seq = 0;
        uint32_t meshId = kInvalidGpuDrivenSlot;
        uint32_t materialId = kInvalidGpuDrivenSlot;
        uint32_t pipelineId = 0;
    };

    static uint64_t makeKey(uint32_t seq, uint32_t meshId, uint32_t materialId,
                            uint32_t pipelineId) {
        return (uint64_t(pipelineId & 0xFF) << 56) |
               (uint64_t(materialId & 0xFFFF) << 40) |
               (uint64_t(meshId & 0xFFFF) << 24) | (seq & 0xFFFFFF);
    }

    std::vector<Entry> entries_;
    std::vector<uint32_t> order_;
    std::vector<GpuIndirectCommand> commands_;
};

}  // namespace eve::graphics
