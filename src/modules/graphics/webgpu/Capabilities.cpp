#include "graphics/webgpu/Capabilities.h"

#include <algorithm>

namespace eve::graphics::webgpu {

namespace {
constexpr uint32_t kDefaultSampleCount = 4;
}  // namespace

WGPUTextureFormat Capabilities::querySurfaceFormat(wgpu::Adapter &adapter,
                                                   wgpu::Surface &surface,
                                                   WGPUTextureFormat defaultFormat) {
    if (!surface || !adapter) return defaultFormat;
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(surface.Get(), adapter.Get(), &caps) == WGPUStatus_Success &&
        caps.formatCount > 0) {
        WGPUTextureFormat fmt = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return fmt;
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return defaultFormat;
}

void Capabilities::capture(wgpu::Adapter &adapter, wgpu::Device &device) {
    // Device limits give the authoritative budget for the feature gates the
    // backend actually uses (sample count, bind groups, dynamic uniform
    // buffers, texture dimension). Fall back to the adapter limits when the
    // device has not been created yet.
    wgpu::Limits limits{};
    bool ok = false;
    if (device) {
        ok = (device.GetLimits(&limits) == wgpu::Status::Success);
    } else if (adapter) {
        ok = (adapter.GetLimits(&limits) == wgpu::Status::Success);
    }
    if (ok) {
        if (limits.maxTextureDimension2D != wgpu::kLimitU32Undefined)
            maxTextureDim2D_ = limits.maxTextureDimension2D;
        if (limits.maxBindGroups != wgpu::kLimitU32Undefined)
            maxBindGroups_ = limits.maxBindGroups;
        if (limits.maxDynamicUniformBuffersPerPipelineLayout != wgpu::kLimitU32Undefined)
            maxDynamicUniformBuffers_ = limits.maxDynamicUniformBuffersPerPipelineLayout;
        // Max sample count is not a single limit; cap conservatively at 4.
        maxSampleCount_ = std::min<uint32_t>(maxSampleCount_, 4u);
    }
    captured_ = true;
}

bool Capabilities::supportsSampleCount(uint32_t count) const {
    if (!captured_) return count <= kDefaultSampleCount;
    if (count <= 1) return true;
    return count <= maxSampleCount_;
}

bool Capabilities::supportsFormat(WGPUTextureFormat format) const {
    // The formats the backend creates are core WebGPU renderable formats that
    // the fixed pipelines require; report them as supported.
    switch (format) {
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_Depth32Float:
        return true;
    default:
        return false;
    }
}

}  // namespace eve::graphics::webgpu