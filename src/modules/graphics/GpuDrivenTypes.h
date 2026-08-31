#pragma once

#include "common/GpuResidentBufferView.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace eve::graphics {

/**
 * @brief GPU-driven rendering shared constants + std430 GPU layouts.
 *
 * These layouts are mirrored by the GLSL shaders that consume the bindless
 * table (mesh3d_gpudriven.*) and by the compute passes (stage 2+). They live
 * in the backend-independent graphics namespace so RenderSystem3D can build
 * them without depending on the Vulkan backend.
 */

constexpr uint32_t kInvalidGpuDrivenSlot = 0xFFFFFFFFu;
constexpr uint32_t kMaxGpuDrivenTextures = 1024;  // sampler2D array slots (desktop limit)
constexpr uint32_t kMaxGpuDrivenCubemaps = 64;    // samplerCube array slots

using eve::GpuResidentBackend;
using eve::GpuResidentBufferView;
using eve::kGpuResidentStorageOffsetAlignment;

/** @brief One contiguous mesh/material bucket in a sorted resident instance buffer. */
struct GpuResidentInstanceBucket {
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    uint32_t meshId        = kInvalidGpuDrivenSlot;
    uint32_t materialId    = kInvalidGpuDrivenSlot;
};

/**
 * @brief Direct-render description for a GPU-authored array of GpuInstance records.
 * @ownership buckets and buffer are borrowed for this call; the native buffer remains
 * caller-owned through the frame fence as required by GpuResidentBufferView.
 */
struct GpuResidentInstanceBatch {
    GpuResidentBufferView            buffer;
    const GpuResidentInstanceBucket *buckets       = nullptr;
    uint32_t                         bucketCount   = 0;
    uint32_t                         instanceCount = 0;
};

/** @brief Structured result for direct resident-instance submission. */
enum class GpuResidentSubmitStatus : uint8_t {
    Submitted,
    Unsupported,
    InvalidArgument,
    BackendMismatch,
    ResourceUnavailable,
    CapacityExceeded,
};

/// @brief GPU mesh table record (std430). Mirrors GLSL GpuMeshRecord.
struct GpuMeshRecord {
    glm::vec4 boundsCenterRadius;  // model-space bounding sphere (xyz = center, w = radius)
    uint32_t vertexOffset = 0;     // offset into pooled vertex buffer (vertex count)
    uint32_t vertexCount = 0;
    uint32_t indexOffset = 0;      // offset into pooled index buffer (index count)
    uint32_t indexCount = 0;
    uint32_t indexType = 1;        // 0 = u16, 1 = u32
    uint32_t firstIndex = 0;       // VkDrawIndexedIndirectCommand.firstIndex
    uint32_t vertexBase = 0;       // VkDrawIndexedIndirectCommand.vertexOffset
    uint32_t lodGroupId = kInvalidGpuDrivenSlot;  // stage 2: LOD chain table index
    uint32_t vgAssetId = kInvalidGpuDrivenSlot;   // stage 3: virtual-geometry asset
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
    uint32_t pad3 = 0;
};
static_assert(sizeof(GpuMeshRecord) == 64, "GpuMeshRecord must be 64B (std430 16B aligned)");

/// @brief GPU material table record (std430). Mirrors GLSL GpuMaterialRecord.
struct GpuMaterialRecord {
    glm::vec4 tint;       // rgba
    glm::vec4 pbr;        // x = metallic, y = roughness, z = receiveShadow, w = receiveLight
    glm::vec4 texBomb;    // x = cellScale, y = strength, z = rotAmount
    glm::vec4 parallax;   // x = scale, y = minLayers, z = maxLayers
    uint32_t textureSlots[4];  // xyzw = albedo / normal / height / env bindless slots
    uint32_t shadingModel = 0; // 0 = pbr, 1 = unlit, 2 = hair, 3 = custom
    uint32_t flags = 0;        // bit0 castShadow, bit1 castOcclusion
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};
static_assert(sizeof(GpuMaterialRecord) == 96, "GpuMaterialRecord must be 96B (std430 16B aligned)");

/// @brief Per-instance GPU record (std430). Mirrors GLSL GpuInstance.
struct GpuInstance {
    glm::mat4 model;
    uint32_t meshId = kInvalidGpuDrivenSlot;      // -> GpuMeshRecord table
    uint32_t materialId = kInvalidGpuDrivenSlot;  // -> GpuMaterialRecord table
    uint32_t flags = 0;                           // bit0 castShadow, bit1 receiveShadow, bit2 castOcclusion
    uint32_t lodGroupId = kInvalidGpuDrivenSlot;  // stage 2
    glm::uvec4 reflectionProbeSlots{kInvalidGpuDrivenSlot, kInvalidGpuDrivenSlot, 0u, 0u};
    glm::vec4 reflectionProbeCenter[2]{};  // xyz = center, w = intensity
    glm::vec4 reflectionProbeExtent[2]{};  // xyz = extent, w = blend distance
};
static_assert(sizeof(GpuInstance) == 160, "GpuInstance must be 160B (std430)");

/// @brief Indirect draw command; layout identical to VkDrawIndexedIndirectCommand.
struct GpuIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    uint32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(GpuIndirectCommand) == 20, "GpuIndirectCommand must match VkDrawIndexedIndirectCommand (20B)");

/// @brief GPU-packed cluster node (std430, 4 x uvec4). Mirrors the
/// virtualgeometry module's VgGpuCluster layout.
struct GpuVgCluster {
    std::uint32_t u0[4];  // bounds: f32(cx), f32(cy), f32(cz), f32(r)
    std::uint32_t u1[4];  // triStart, triCount, lodLevel, parent
    std::uint32_t u2[4];  // f32(errorR), f32(errorRScreen), childCount, 0
    std::uint32_t u3[4];  // children[4]
};
static_assert(sizeof(GpuVgCluster) == 64, "GpuVgCluster must be 64B (4 x uvec4)");

/// @brief Neutral GPU upload for one virtual-geometry asset. Raw arrays so the
/// graphics module does not depend on the virtualgeometry module.
struct GpuVgAssetUpload {
    const float *positions = nullptr;      // xyz packed, 3 * vertexCount
    int vertexCount = 0;
    const float *normals = nullptr;        // optional xyz packed
    const std::uint32_t *triangles = nullptr;  // global triangle stream (indices)
    int triangleCount = 0;                 // index count (multiple of 3)
    const GpuVgCluster *clusters = nullptr;
    int clusterCount = 0;
};

}  // namespace eve::graphics
