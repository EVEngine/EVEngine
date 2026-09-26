#pragma once

#include "common/Export.h"

#include <cstdint>

namespace eve::graphics {

/**
 * @brief Hardware ray-tracing capabilities probed once at Vulkan device creation.
 *
 * When `available` is false the optional `graphics_raytracing` module still
 * links, but every fallible RT operation returns `StatusCode::Unsupported`.
 * Screen-space reflections remain the portable fallback.
 */
struct EVENGINE_API_BACKENDS_INLINE RayTracingCaps {
    bool     available                  = false;  ///< Full KHR RT pipeline path usable
    bool     accelerationStructure      = false;
    bool     rayTracingPipeline         = false;
    bool     bufferDeviceAddress        = false;
    bool     rayQuery                   = false;  ///< Optional VK_KHR_ray_query
    uint32_t shaderGroupHandleSize      = 0;
    uint32_t shaderGroupBaseAlignment   = 0;
    uint32_t shaderGroupHandleAlignment = 0;
    uint32_t maxRecursionDepth          = 0;

    /** @brief True when BLAS/TLAS + ray-tracing pipelines can be created. */
    bool rayTracingAvailable() const {
        return available && accelerationStructure && rayTracingPipeline && bufferDeviceAddress;
    }
};

}  // namespace eve::graphics
