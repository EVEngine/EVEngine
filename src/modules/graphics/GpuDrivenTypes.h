#pragma once

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
    uint32_t pad0 = 0;
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
};
static_assert(sizeof(GpuInstance) == 80, "GpuInstance must be 80B (std430)");

/// @brief Indirect draw command; layout identical to VkDrawIndexedIndirectCommand.
struct GpuIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    uint32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(GpuIndirectCommand) == 20, "GpuIndirectCommand must match VkDrawIndexedIndirectCommand (20B)");

}  // namespace eve::graphics
