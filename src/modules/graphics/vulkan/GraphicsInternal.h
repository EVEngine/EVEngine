#pragma once

// Shared implementation helpers for the Vulkan graphics backend.
// Re-generated from the merged dev single-TU Graphics.cpp (pure move).
// Anonymous namespace: each TU gets its own internal copy.

#include "graphics/vulkan/Graphics.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace eve::graphics::vulkan {
namespace {


constexpr auto kHostVisibleCoherent = vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent;

template <typename T, size_t N>
std::vector<T> embeddedSpirv(const T (&words)[N]) {
    return {words, words + N};
}

template <typename Slots>
auto &currentSlot(Slots &slots, size_t slotCount, size_t slotIndex) {
    if (slots.size() != slotCount) slots.resize(slotCount);
    return slots[slotIndex];
}

template <typename FrameBuffers>
void releaseFrame2dBuffers(FrameBuffers &buffers) {
    for (auto &buffer : buffers.solidBufs) buffer.release();
    buffers.solidBufs.clear();
    for (auto &buffer : buffers.texBufs) buffer.release();
    buffers.texBufs.clear();
}

void setViewportAndScissor(vk::CommandBuffer cb, uint32_t width, uint32_t height) {
    const vk::Viewport viewport{0.f, 0.f, float(width), float(height), 0.f, 1.f};
    const vk::Rect2D scissor{{0, 0}, {width, height}};
    cb.setViewport(0, 1, &viewport);
    cb.setScissor(0, 1, &scissor);
}

vk::PipelineLayout createPipelineLayout(vkb::Device &device,
                                        vk::DescriptorSetLayout setLayout = {},
                                        const vk::PushConstantRange *pushConstant = nullptr) {
    vk::PipelineLayoutCreateInfo info{};
    if (setLayout) {
        info.setLayoutCount = 1;
        info.pSetLayouts = &setLayout;
    }
    if (pushConstant) {
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = pushConstant;
    }
    return device->createPipelineLayout(info);
}

vk::PushConstantRange pushConstantRange(vk::ShaderStageFlags stages, uint32_t size) {
    vk::PushConstantRange range{};
    range.stageFlags = stages;
    range.size = size;
    return range;
}

void destroySampler(vkb::Device &device, vk::Sampler &sampler) {
    if (!sampler) return;
    device->destroySampler(sampler);
    sampler = vk::Sampler{};
}

void destroyPipeline(vkb::Device &device, vk::Pipeline &pipeline) {
    if (!pipeline) return;
    device->destroyPipeline(pipeline);
    pipeline = vk::Pipeline{};
}

void destroyPipelineLayout(vkb::Device &device, vk::PipelineLayout &layout) {
    if (!layout) return;
    device->destroyPipelineLayout(layout);
    layout = vk::PipelineLayout{};
}

/** Latest written vertex copy: the dynamic ring, or the static buffer for
 *  meshes that are never updated. */
vkb::HostVertexBuffer &meshDrawVertices(GpuMesh &mesh) {
    if (!mesh.dynamic) return mesh.vertices;
    const size_t slot = size_t((mesh.dynamicWriteCount - 1) % GpuMesh::kDynamicVertexCopies);
    return mesh.dynVertices[slot];
}

vkb::GenericBuffer &meshDrawIndices(GpuMesh &mesh) {
    if (!mesh.dynamic) return mesh.indices;
    const size_t slot = size_t((mesh.dynamicWriteCount - 1) % GpuMesh::kDynamicVertexCopies);
    return mesh.dynIndices[slot];
}

void drawIndexedMesh(vk::CommandBuffer cb, GpuMesh &mesh) {
    const vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, meshDrawVertices(mesh), &offset);
    cb.bindIndexBuffer(meshDrawIndices(mesh).buffer, 0, mesh.indexType);
    cb.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
}

/** Switch a mesh to the dynamic ring on first update; take a CPU copy of the
 *  static index buffer so every ring slot can be populated (also normalizes
 *  16-bit indices to 32-bit). */
void ensureDynamicRing(GpuMesh &gpu) {
    if (gpu.dynamic) return;
    gpu.dynamic = true;
    gpu.dynamicWriteCount = 0;
    gpu.cpuIndices.resize(gpu.indexCount);
    if (gpu.indexCount > 0 && gpu.indices.buffer) {
        void *ptr = gpu.indices.map();
        if (gpu.indexType == vk::IndexType::eUint16) {
            const auto *src = static_cast<const uint16_t *>(ptr);
            for (uint32_t i = 0; i < gpu.indexCount; ++i)
                gpu.cpuIndices[size_t(i)] = src[i];
        } else {
            std::memcpy(gpu.cpuIndices.data(), ptr,
                        size_t(gpu.indexCount) * sizeof(uint32_t));
        }
        gpu.indices.unmap();
    }
    gpu.indexType = vk::IndexType::eUint32;
}

/** Write one ring copy. No device-wide wait: the slot being overwritten is
 *  kDynamicVertexCopies frames old, i.e. past its in-flight window. */
void writeDynamicMesh(GpuMesh &gpu, const std::vector<MeshVertex> &verts, vkb::Device &device,
                      vkb::FrameSlot frame, const uint32_t *indices, int indexCount) {
    const size_t slot = size_t(gpu.dynamicWriteCount % GpuMesh::kDynamicVertexCopies);
    gpu.vertexCount = uint32_t(verts.size());
    gpu.dynVertices[slot].allocate<MeshVertex>(frame, device, verts);
    if (indices && indexCount > 0) {
        gpu.cpuIndices.assign(indices, indices + indexCount);
        gpu.indexCount = uint32_t(indexCount);
    }
    if (!gpu.cpuIndices.empty()) {
        auto &ib = gpu.dynIndices[slot];
        ib.allocate(frame, device, vk::BufferUsageFlagBits::eIndexBuffer,
                    vk::DeviceSize(gpu.cpuIndices.size()) * sizeof(uint32_t),
                    kHostVisibleCoherent);
        ib.updateLocal(frame, gpu.cpuIndices.data(),
                       vk::DeviceSize(gpu.cpuIndices.size()) * sizeof(uint32_t));
    }
    ++gpu.dynamicWriteCount;
}

std::unique_ptr<GpuMesh> uploadGpuMesh(vkb::Device &device, vkb::FrameSlot frame,
                                       const std::vector<MeshVertex> &vertices,
                                       const std::vector<uint32_t> &indices) {
    auto gpu = std::make_unique<GpuMesh>();
    gpu->vertexCount = uint32_t(vertices.size());
    gpu->vertices.allocate<MeshVertex>(frame, device, vertices);
    gpu->indices.allocate(frame, device, vk::BufferUsageFlagBits::eIndexBuffer,
                          indices.size() * sizeof(uint32_t), kHostVisibleCoherent);
    gpu->indices.updateLocal(frame, indices.data(), indices.size() * sizeof(uint32_t));
    gpu->indexCount = uint32_t(indices.size());
    return gpu;
}

/** 16-bit index upload (halves index memory for meshes with <= 65535 vertices). */
std::unique_ptr<GpuMesh> uploadGpuMesh16(vkb::Device &device, vkb::FrameSlot frame,
                                         const std::vector<MeshVertex> &vertices,
                                         const std::vector<uint16_t> &indices) {
    auto gpu = std::make_unique<GpuMesh>();
    gpu->vertexCount = uint32_t(vertices.size());
    gpu->vertices.allocate<MeshVertex>(frame, device, vertices);
    gpu->indices.allocate(frame, device, vk::BufferUsageFlagBits::eIndexBuffer,
                          indices.size() * sizeof(uint16_t), kHostVisibleCoherent);
    gpu->indices.updateLocal(frame, indices.data(), indices.size() * sizeof(uint16_t));
    gpu->indexCount = uint32_t(indices.size());
    gpu->indexType = vk::IndexType::eUint16;
    return gpu;
}

std::unique_ptr<Mesh> makeMeshHandle(GpuMesh &gpu) {
    auto mesh = std::make_unique<Mesh>();
    mesh->indexCount = int(gpu.indexCount);
    mesh->gpuVertexCount = int(gpu.vertexCount);
    mesh->gpuHandle = &gpu;
    return mesh;
}

void assignMeshBounds(Mesh *mesh, const std::vector<MeshVertex> &verts) {
    if (!mesh || verts.empty()) return;
    glm::vec3 c(0.f);
    for (const auto &v : verts) c += v.pos;
    c /= float(verts.size());
    mesh->boundsCx = c.x;
    mesh->boundsCy = c.y;
    mesh->boundsCz = c.z;
    float r = 0.f;
    for (const auto &v : verts) {
        const glm::vec3 d = v.pos - c;
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len > r) r = len;
    }
    // Same degenerate-mesh rule as Mesh::computeBounds: keep a tiny non-zero
    // sphere so hasBounds() stays meaningful for culling.
    mesh->boundsRadius = r > 0.f ? r : 1e-4f;
}

/** Host-coherent write at a byte offset into a mapped ring buffer. */
void updateRingLocal(vkb::GenericBuffer &ring, vk::DeviceSize byteOffset, const void *data,
                     vk::DeviceSize bytes) {
    if (!ring.buffer || !data || bytes == 0) return;
    void *ptr = ring.map();
    std::memcpy(static_cast<char *>(ptr) + byteOffset, data, size_t(bytes));
    ring.unmap();
}

template <typename T>
inline T alignUpValue(T value, T align) {
    return align > 0 ? (value + align - 1) / align * align : value;
}

vk::PipelineColorBlendAttachmentState makeBlendAttachment(BlendMode mode) {
    vk::PipelineColorBlendAttachmentState att{};
    att.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    att.blendEnable = true;
    if (mode == BlendMode::Additive) {
        att.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        att.dstColorBlendFactor = vk::BlendFactor::eOne;
        att.colorBlendOp = vk::BlendOp::eAdd;
        att.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        att.dstAlphaBlendFactor = vk::BlendFactor::eOne;
        att.alphaBlendOp = vk::BlendOp::eAdd;
    } else if (mode == BlendMode::Premultiplied) {
        att.srcColorBlendFactor = vk::BlendFactor::eOne;
        att.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.colorBlendOp = vk::BlendOp::eAdd;
        att.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        att.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.alphaBlendOp = vk::BlendOp::eAdd;
    } else if (mode == BlendMode::Multiply) {
        att.srcColorBlendFactor = vk::BlendFactor::eDstColor;
        att.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.colorBlendOp = vk::BlendOp::eAdd;
        att.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        att.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.alphaBlendOp = vk::BlendOp::eAdd;
    } else {
        att.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        att.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.colorBlendOp = vk::BlendOp::eAdd;
        att.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        att.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        att.alphaBlendOp = vk::BlendOp::eAdd;
    }
    return att;
}

class ShaderModulePair {
public:
    ShaderModulePair(vkb::Device &device, const std::vector<uint32_t> &vert,
                     const std::vector<uint32_t> &frag)
        : device(device),
          vert(vkb::PipelineBuilder::createShaderModule(device.instance, vert)),
          frag(vkb::PipelineBuilder::createShaderModule(device.instance, frag)) {}

    ~ShaderModulePair() {
        device->destroyShaderModule(vert);
        device->destroyShaderModule(frag);
    }

    ShaderModulePair(const ShaderModulePair &) = delete;
    ShaderModulePair &operator=(const ShaderModulePair &) = delete;

    vkb::Device &device;
    vk::ShaderModule vert;
    vk::ShaderModule frag;
};



vk::Format pickGBufferColorFormat(vkb::Device &device) {
    (void)device;
    return vk::Format::eR8G8B8A8Unorm;
}

vk::SampleCountFlagBits sampleCountFlagFor(int samples) {
    if (samples >= 8) return vk::SampleCountFlagBits::e8;
    if (samples >= 4) return vk::SampleCountFlagBits::e4;
    if (samples >= 2) return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}



uint32_t rgba8MipBytes(uint32_t width, uint32_t height) {
    // Matches VKBuilder GenericImage::upload packing for eR8G8B8A8Unorm.
    return 4u * width * height;
}

void appendBoxFilteredMip(std::vector<uint8_t> &out, const uint8_t *src, uint32_t srcW,
                          uint32_t srcH, uint32_t dstW, uint32_t dstH) {
    const size_t base = out.size();
    out.resize(base + size_t(rgba8MipBytes(dstW, dstH)));
    uint8_t *dst = out.data() + base;
    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t y0 = std::min(y * 2u, srcH - 1u);
        const uint32_t y1 = std::min(y0 + 1u, srcH - 1u);
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = std::min(x * 2u, srcW - 1u);
            const uint32_t x1 = std::min(x0 + 1u, srcW - 1u);
            uint32_t acc[4] = {0, 0, 0, 0};
            const uint32_t samples[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
            for (const auto &s : samples) {
                const size_t i = (size_t(s[1]) * srcW + s[0]) * 4u;
                acc[0] += src[i + 0];
                acc[1] += src[i + 1];
                acc[2] += src[i + 2];
                acc[3] += src[i + 3];
            }
            const size_t o = (size_t(y) * dstW + x) * 4u;
            dst[o + 0] = static_cast<uint8_t>((acc[0] + 2u) / 4u);
            dst[o + 1] = static_cast<uint8_t>((acc[1] + 2u) / 4u);
            dst[o + 2] = static_cast<uint8_t>((acc[2] + 2u) / 4u);
            dst[o + 3] = static_cast<uint8_t>((acc[3] + 2u) / 4u);
        }
    }
}

/** Packed mip chain for one 2D layer (base + downsampled levels). */
std::vector<uint8_t> buildMipChain2D(const uint8_t *rgba, uint32_t width, uint32_t height,
                                     uint32_t mipLevels) {
    std::vector<uint8_t> packed;
    packed.reserve(size_t(width) * size_t(height) * 4u * 2u);
    packed.insert(packed.end(), rgba, rgba + size_t(rgba8MipBytes(width, height)));

    uint32_t srcW = width;
    uint32_t srcH = height;
    size_t srcOffset = 0;
    for (uint32_t level = 1; level < mipLevels; ++level) {
        const uint32_t dstW = std::max(srcW >> 1, 1u);
        const uint32_t dstH = std::max(srcH >> 1, 1u);
        const uint8_t *src = packed.data() + srcOffset;
        srcOffset = packed.size();
        appendBoxFilteredMip(packed, src, srcW, srcH, dstW, dstH);
        srcW = dstW;
        srcH = dstH;
    }
    return packed;
}

/**
 * Cubemap packed as VKBuilder expects: for each mip, for each face (+X..-Z).
 * Input faces are contiguous full-res faces.
 */
std::vector<uint8_t> buildMipChainCube(const uint8_t *rgbaFaces, uint32_t faceSize,
                                       uint32_t mipLevels) {
    const uint32_t faceBytes = rgba8MipBytes(faceSize, faceSize);
    std::vector<std::vector<uint8_t>> faceChains(6);
    for (uint32_t f = 0; f < 6; ++f) {
        faceChains[f] = buildMipChain2D(rgbaFaces + size_t(f) * faceBytes, faceSize, faceSize,
                                        mipLevels);
    }

    std::vector<uint8_t> packed;
    uint32_t w = faceSize;
    uint32_t h = faceSize;
    size_t faceOffsets[6] = {0, 0, 0, 0, 0, 0};
    for (uint32_t level = 0; level < mipLevels; ++level) {
        const uint32_t levelBytes = rgba8MipBytes(w, h);
        for (uint32_t f = 0; f < 6; ++f) {
            const uint8_t *src = faceChains[f].data() + faceOffsets[f];
            packed.insert(packed.end(), src, src + levelBytes);
            faceOffsets[f] += levelBytes;
        }
        w = std::max(w >> 1, 1u);
        h = std::max(h >> 1, 1u);
    }
    return packed;
}

TextureCreateInfo normalizeTextureInfo(TextureCreateInfo info) {
    if (info.generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled)
        info.sampler.mipmap = MipmapMode::Linear;
    if (info.sampler.maxAnisotropy < 1.f) info.sampler.maxAnisotropy = 1.f;
    return info;
}



std::string normalizeTexPath(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

}  // namespace
}  // namespace eve::graphics::vulkan
