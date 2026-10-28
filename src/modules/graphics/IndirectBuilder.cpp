#include "graphics/IndirectBuilder.h"

namespace eve::graphics {

void IndirectBuilder::reset() {
    entries_.clear();
    order_.clear();
    commands_.clear();
}

void IndirectBuilder::add(uint32_t seq, uint32_t meshId, uint32_t materialId,
                          uint32_t pipelineId) {
    entries_.push_back(Entry{makeKey(seq, meshId, materialId, pipelineId), seq, meshId,
                             materialId, pipelineId});
}

uint32_t IndirectBuilder::build(const std::vector<GpuMeshRecord> &meshTable) {
    order_.clear();
    commands_.clear();
    order_.reserve(entries_.size());

    std::vector<uint32_t> idx(entries_.size());
    for (uint32_t i = 0; i < entries_.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [this](uint32_t a, uint32_t b) {
        const Entry &ea = entries_[a];
        const Entry &eb = entries_[b];
        if (ea.key != eb.key) return ea.key < eb.key;
        return ea.seq < eb.seq;
    });
    for (uint32_t i : idx) order_.push_back(i);

    size_t i = 0;
    while (i < idx.size()) {
        const Entry &first = entries_[idx[i]];
        size_t j = i + 1;
        while (j < idx.size()) {
            const Entry &e = entries_[idx[j]];
            if (e.pipelineId != first.pipelineId || e.materialId != first.materialId ||
                e.meshId != first.meshId)
                break;
            ++j;
        }
        if (first.meshId < meshTable.size()) {
            const GpuMeshRecord &m = meshTable[first.meshId];
            if (m.indexCount > 0) {
                GpuIndirectCommand cmd{};
                cmd.indexCount = m.indexCount;
                cmd.instanceCount = uint32_t(j - i);
                cmd.firstIndex = m.firstIndex;
                cmd.vertexOffset = m.vertexBase;
                cmd.firstInstance = uint32_t(i);  // position in the sorted instance buffer
                commands_.push_back(cmd);
            }
        }
        i = j;
    }
    return uint32_t(commands_.size());
}

}  // namespace eve::graphics
