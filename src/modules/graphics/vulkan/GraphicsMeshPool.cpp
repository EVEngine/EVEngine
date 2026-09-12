#include <algorithm>
#include <cstring>
#include <limits>
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
void Graphics::ensureGpuVertexPool() {
    if (gpuVertexPool_.positions.buffer) return;
    constexpr uint32_t kInitialVertices = 256u << 10;  // 256k verts (~11 MB total)
    constexpr uint32_t kInitialIndices  = 768u << 10;  // 768k u32 indices (3 MB)
    const auto         hostMem          = kHostVisibleCoherent;
    gpuVertexPool_.positions            = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec4), hostMem);
    gpuVertexPool_.normals              = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec4), hostMem);
    gpuVertexPool_.uvs                  = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialVertices * sizeof(glm::vec2), hostMem);
    gpuVertexPool_.indices              = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                                             kInitialIndices * sizeof(uint32_t), hostMem);
    gpuVertexPool_.vertexCount          = 0;
    gpuVertexPool_.indexCount           = 0;
}

void Graphics::growGpuVertexPool(uint32_t needVertices, uint32_t needIndices) {
    // Pool growth reallocates the buffers; a pending frame may still be reading
    // them, so drain the GPU first (rare path: first-time mesh registration).
    device->waitIdle();
    const uint32_t oldVerts = gpuVertexPool_.vertexCount;
    const uint32_t oldInds  = gpuVertexPool_.indexCount;
    const auto     hostMem  = kHostVisibleCoherent;
    auto           copyInto = [&](vkb::GenericBuffer& dst, vkb::GenericBuffer& src, vk::DeviceSize oldBytes,
                        vk::DeviceSize newBytes) {
        vkb::GenericBuffer grown(device, vk::BufferUsageFlagBits::eStorageBuffer, newBytes, hostMem);
        if (oldBytes > 0) {
            void* srcMap = src.map();
            void* dstMap = grown.map();
            std::memcpy(dstMap, srcMap, oldBytes);
            grown.unmap();
            src.unmap();
        }
        src.release();
        dst = std::move(grown);
    };
    // Leave room after a large first insertion. Exact-fit growth made the next
    // tiny mesh copy the entire large allocation immediately afterwards.
    const auto capacity = [](uint32_t needed) {
        return uint32_t(std::min(uint64_t(needed) * 2u, uint64_t(UINT32_MAX)));
    };
    const uint32_t newVerts =
        std::max(capacity(needVertices), uint32_t(gpuVertexPool_.positions.size / sizeof(glm::vec4)));
    const uint32_t newInds = std::max(capacity(needIndices), uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t)));
    copyInto(gpuVertexPool_.positions, gpuVertexPool_.positions, vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.normals, gpuVertexPool_.normals, vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.uvs, gpuVertexPool_.uvs, vk::DeviceSize(oldVerts) * sizeof(glm::vec2),
             vk::DeviceSize(newVerts) * sizeof(glm::vec2));
    copyInto(gpuVertexPool_.indices, gpuVertexPool_.indices, vk::DeviceSize(oldInds) * sizeof(uint32_t),
             vk::DeviceSize(newInds) * sizeof(uint32_t));
    bindGpuVertexPoolBindless();
}

void Graphics::bindGpuVertexPoolBindless() {
    if (bindlessSets_.empty() || !gpuVertexPool_.positions.buffer) return;
    auto bufWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize size) {
        vk::DescriptorBufferInfo info{buffer, 0, size};
        vk::WriteDescriptorSet   w{};
        w.dstSet          = set;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo     = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    const uint32_t cap = [&]() {
        const vk::DeviceSize bytes = gpuVertexPool_.positions.size;
        return uint32_t(bytes / sizeof(glm::vec4));
    }();
    const uint32_t indCap = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    for (vk::DescriptorSet set : bindlessSets_) {
        bufWrite(set, 18, gpuVertexPool_.positions.buffer, vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 19, gpuVertexPool_.normals.buffer, vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 20, gpuVertexPool_.uvs.buffer, vk::DeviceSize(cap) * sizeof(glm::vec2));
        bufWrite(set, 21, gpuVertexPool_.indices.buffer, vk::DeviceSize(indCap) * sizeof(uint32_t));
    }
}

void Graphics::appendGpuMeshToPool(GpuMesh& gpu, const std::vector<MeshVertex>* vertices,
                                   const std::vector<uint32_t>* indices) {
    if (!gpu.vertices.buffer || !gpu.indices.buffer) return;
    ensureGpuVertexPool();
    const uint32_t nVerts = gpu.record.vertexCount;
    const uint32_t nInds  = gpu.record.indexCount;
    if (nVerts == 0 || nInds == 0) return;
    const uint32_t newVerts = gpuVertexPool_.vertexCount + nVerts;
    const uint32_t newInds  = gpuVertexPool_.indexCount + nInds;
    const uint32_t capVerts = uint32_t(gpuVertexPool_.positions.size / sizeof(glm::vec4));
    const uint32_t capInds  = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    if (newVerts > capVerts || newInds > capInds) {
        growGpuVertexPool(newVerts, newInds);
    }

    const void* vMap = vertices ? vertices->data() : gpu.vertices.map();
    const void* iMap = indices ? indices->data() : gpu.indices.map();
    if (!vMap || !iMap) return;
    auto* verts  = static_cast<const MeshVertex*>(vMap);
    auto* posDst = static_cast<glm::vec4*>(gpuVertexPool_.positions.map());
    auto* nrmDst = static_cast<glm::vec4*>(gpuVertexPool_.normals.map());
    auto* uvDst  = static_cast<glm::vec2*>(gpuVertexPool_.uvs.map());
    auto* idxDst = static_cast<uint32_t*>(gpuVertexPool_.indices.map());
    if (!posDst || !nrmDst || !uvDst || !idxDst) {
        if (!vertices) gpu.vertices.unmap();
        if (!indices) gpu.indices.unmap();
        return;
    }
    posDst += gpuVertexPool_.vertexCount;
    nrmDst += gpuVertexPool_.vertexCount;
    uvDst += gpuVertexPool_.vertexCount;
    idxDst += gpuVertexPool_.indexCount;
    for (uint32_t i = 0; i < nVerts; ++i) {
        posDst[i] = glm::vec4(verts[i].pos, 0.f);
        nrmDst[i] = glm::vec4(verts[i].normal, 0.f);
        uvDst[i]  = verts[i].uv;
    }
    if (!indices && gpu.indexType == vk::IndexType::eUint16) {
        const auto* src16 = static_cast<const uint16_t*>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = uint32_t(src16[i]);
    } else {
        const auto* src32 = static_cast<const uint32_t*>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = src32[i];
    }
    gpuVertexPool_.positions.unmap();
    gpuVertexPool_.normals.unmap();
    gpuVertexPool_.uvs.unmap();
    gpuVertexPool_.indices.unmap();
    if (!vertices) gpu.vertices.unmap();
    if (!indices) gpu.indices.unmap();

    // Pool offsets are vertex/index counts, resolved by the vis shaders.
    gpu.record.vertexOffset    = gpuVertexPool_.vertexCount;
    gpu.record.indexOffset     = gpuVertexPool_.indexCount;
    gpu.record.firstIndex      = 0;  // vis pass draws non-indexed from the pool
    gpu.record.vertexBase      = 0;
    gpuVertexPool_.vertexCount = newVerts;
    gpuVertexPool_.indexCount  = newInds;
}

uint32_t Graphics::registerMeshRecord(GpuMesh* gpu, const std::vector<MeshVertex>* vertices,
                                      const std::vector<uint32_t>* indices) {
    if (!gpu) return kInvalidBindlessSlot;
    if (gpu->gpuRecordIndex != kInvalidBindlessSlot) return gpu->gpuRecordIndex;
    if (meshTableRecords_.size() >= meshTableCapacity_) return kInvalidBindlessSlot;
    // dev's mesh factories do not populate GpuMeshRecord; build it on first
    // registration from the host-visible buffers (bounds + ranges).
    if (gpu->record.vertexCount == 0 && gpu->vertices.buffer) {
        const uint32_t vertexCount = uint32_t(gpu->vertices.size / sizeof(MeshVertex));
        gpu->record.vertexCount    = vertexCount;
        gpu->record.indexCount     = gpu->indexCount;
        gpu->record.indexType      = gpu->indexType == vk::IndexType::eUint16 ? 0u : 1u;
        const void* map            = vertices ? vertices->data() : gpu->vertices.map();
        if (map && vertexCount > 0) {
            const auto* verts = static_cast<const MeshVertex*>(map);
            glm::vec3   minv(1e30f), maxv(-1e30f);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                minv = glm::min(minv, verts[i].pos);
                maxv = glm::max(maxv, verts[i].pos);
            }
            const glm::vec3 center = (minv + maxv) * 0.5f;
            float           radius = 0.f;
            for (uint32_t i = 0; i < vertexCount; ++i) radius = std::max(radius, glm::length(verts[i].pos - center));
            gpu->record.boundsCenterRadius = glm::vec4(center, radius);
        }
        if (map && !vertices) gpu->vertices.unmap();
    }
    // Stage 3: lazily pool the mesh's vertices/indices so the vis resolve can
    // fetch attributes by (pool offset + triangle + barycentric).
    appendGpuMeshToPool(*gpu, vertices, indices);
    const uint32_t idx = uint32_t(meshTableRecords_.size());
    meshTableRecords_.push_back(gpu->record);
    meshRecordOwners_.push_back(gpu);
    gpu->gpuRecordIndex = idx;
    syncMeshTable();
    return idx;
}


}  // namespace eve::graphics::vulkan
