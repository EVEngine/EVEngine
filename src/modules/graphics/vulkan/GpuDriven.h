#pragma once

#include "graphics/GpuDrivenTypes.h"

namespace eve::graphics::vulkan {

/**
 * @brief GPU-driven rendering shared constants + std430 GPU layouts.
 *
 * These layouts are mirrored by the GLSL shaders that consume the bindless
 * table (mesh3d_gpudriven.*) and by the compute passes (stage 2+). Keep
 * alignment rules in sync with the GLSL structs.
 */

using GpuMeshRecord = eve::graphics::GpuMeshRecord;
using GpuMaterialRecord = eve::graphics::GpuMaterialRecord;
using GpuInstance = eve::graphics::GpuInstance;
using GpuIndirectCommand = eve::graphics::GpuIndirectCommand;
constexpr uint32_t kMaxBindlessTextures = eve::graphics::kMaxGpuDrivenTextures;
constexpr uint32_t kMaxBindlessCubemaps = eve::graphics::kMaxGpuDrivenCubemaps;
constexpr uint32_t kMaxBindlessSsbo = 64;  // storage buffer table slots
constexpr uint32_t kInvalidBindlessSlot = eve::graphics::kInvalidGpuDrivenSlot;

/// @brief Capabilities required for the GPU-driven path. All must be true for
/// `gpuDrivenAvailable()`. Probing happens once at device creation.
struct GpuDrivenCaps {
    bool api12 = false;
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool descriptorIndexing = false;
    bool shaderSampledImageArrayDynamicIndexing = false;
    bool samplerArrayCapacity = false;  // maxPerStageDescriptorSamplers >= array size

    bool gpuDrivenAvailable() const {
        // NOTE: the Vulkan SDK headers used by this toolchain omit the core
        // `drawIndirect` bit from VkPhysicalDeviceFeatures, so it is assumed
        // supported (true on every real GPU) and left untouched. The engine's
        // glslc build lacks GL_EXT_nonuniform_qualifier, so the bindless path
        // uses fixed-size descriptor arrays + dynamic indexing (legal because
        // material/texture indices are uniform within each indirect draw).
        // UPDATE_AFTER_BIND and the 1.2 feature chain are intentionally NOT
        // required: the AMD driver in this environment hangs on large
        // update-after-bind descriptor writes, and texture registrations are
        // applied outside the render pass instead.
        return shaderSampledImageArrayDynamicIndexing && samplerArrayCapacity;
    }
};

}  // namespace eve::graphics::vulkan
