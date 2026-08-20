#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace eve::graphics::vulkan {

/**
 * @brief GPU-driven rendering shared constants + std430 GPU layouts.
 *
 * These layouts are mirrored by the GLSL shaders that consume the bindless
 * table (mesh3d_gpudriven.*) and by the compute passes (stage 2+). Keep
 * alignment rules in sync with the GLSL structs.
 */

constexpr uint32_t kMaxBindlessTextures = 4096;  // sampler2D array slots
constexpr uint32_t kMaxBindlessCubemaps = 64;    // samplerCube array slots
constexpr uint32_t kMaxBindlessSsbo = 64;        // storage buffer table slots
constexpr uint32_t kInvalidBindlessSlot = 0xFFFFFFFFu;

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
    uint32_t lodGroupId = kInvalidBindlessSlot;  // stage 2: LOD chain table index
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
    uint32_t meshId = kInvalidBindlessSlot;      // -> GpuMeshRecord table
    uint32_t materialId = kInvalidBindlessSlot;  // -> GpuMaterialRecord table
    uint32_t flags = 0;                          // bit0 castShadow, bit1 receiveShadow, bit2 castOcclusion
    uint32_t lodGroupId = kInvalidBindlessSlot;  // stage 2
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

/// @brief Capabilities required for the GPU-driven path. All must be true for
/// `gpuDrivenAvailable()`. Probing happens once at device creation.
struct GpuDrivenCaps {
    bool api12 = false;
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool descriptorIndexing = false;
    bool runtimeDescriptorArray = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    bool shaderStorageBufferArrayNonUniformIndexing = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingStorageBufferUpdateAfterBind = false;

    bool gpuDrivenAvailable() const {
        // NOTE: the Vulkan SDK headers used by this toolchain omit the core
        // `drawIndirect` bit from VkPhysicalDeviceFeatures, so it is assumed
        // supported (true on every real GPU) and left un-touched. All other
        // features below are queried and enabled explicitly.
        return api12 && drawIndirectCount && multiDrawIndirect && descriptorIndexing &&
               runtimeDescriptorArray &&
               shaderSampledImageArrayNonUniformIndexing &&
               shaderStorageBufferArrayNonUniformIndexing &&
               descriptorBindingPartiallyBound &&
               descriptorBindingSampledImageUpdateAfterBind &&
               descriptorBindingStorageBufferUpdateAfterBind;
    }
};

}  // namespace eve::graphics::vulkan
