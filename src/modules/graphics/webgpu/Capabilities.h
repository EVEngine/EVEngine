#pragma once

#include "graphics/webgpu/wgpu_types.h"

#include <cstdint>

namespace eve::graphics::webgpu {

/**
 * @brief Read-only snapshot of WebGPU adapter/device limits and surface
 * capabilities, queried once after device creation.
 *
 * Mirrors the capability-probe pattern of vk-bootstrap (PhysicalDevice/Device
 * queries) so feature-gated paths (MSAA sample count, formats, bind-group
 * budget) can reject infeasible requests before issuing a GPU call, instead of
 * guessing and crashing at validation time.
 */
class Capabilities {
public:
    /** @brief Default-constructed: all queries report their (conservative) default. */
    Capabilities() = default;

    /**
     * @brief Query the surface's preferred format from the adapter.
     * Falls back to `defaultFormat` when the surface reports none.
     */
    static WGPUTextureFormat querySurfaceFormat(wgpu::Adapter &adapter, wgpu::Surface &surface,
                                                WGPUTextureFormat defaultFormat);

    /**
     * @brief Populate the snapshot from an adapter and device.
     * Call once, right after device creation; keep the returned object.
     */
    void capture(wgpu::Adapter &adapter, wgpu::Device &device);

    /** @brief True when the device supports the given render-target sample count. */
    bool supportsSampleCount(uint32_t count) const;

    /** @brief True when the device advertises the given texture format. */
    bool supportsFormat(WGPUTextureFormat format) const;

    /** @brief Maximum 2D texture dimension the device guarantees. */
    uint32_t maxTextureDimension2D() const { return maxTextureDim2D_; }

    /** @brief Maximum bind group count per pipeline layout. */
    uint32_t maxBindGroups() const { return maxBindGroups_; }

    /** @brief Maximum dynamic uniform buffers per pipeline layout. */
    uint32_t maxDynamicUniformBuffers() const { return maxDynamicUniformBuffers_; }

    /** @brief Maximum sampler anisotropy the device supports (>=1). */
    float maxSamplerAnisotropy() const { return maxAnisotropy_; }

private:
    // Conservative defaults: a value that is never worse than a guaranteed limit
    // so an uncaptured Capabilities still lets valid calls through.
    uint32_t maxTextureDim2D_ = 16384;
    uint32_t maxBindGroups_ = 4;
    uint32_t maxDynamicUniformBuffers_ = 8;
    float maxAnisotropy_ = 16.f;
    uint32_t maxSampleCount_ = 4;
    bool captured_ = false;
};

}  // namespace eve::graphics::webgpu