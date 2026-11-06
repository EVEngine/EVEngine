#pragma once

#include "graphics/webgpu/wgpu_types.h"
#include "graphics/webgpu/Capabilities.h"

#include <cstdint>
#include <string>
#include <utility>

namespace eve::graphics::webgpu {

// Stage objects are produced in a strict order: each request consumes the
// previous stage and yields the next one, so calling a later step without the
// earlier one (or out of order) is a compile error — the vk-bootstrap principle
// applied to WebGPU's async initialization.

/** @brief Instance created, no adapter yet. */
struct InstanceDone {
    wgpu::Instance instance;
};

/** @brief Instance + adapter resolved. */
struct AdapterDone {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
};

/** @brief Instance + adapter + device + queue + capability snapshot. */
struct DeviceDone {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    Capabilities caps;
};

/**
 * @brief Typed, blocking WebGPU initialization flow.
 *
 * Wraps the raw async request callbacks (RequestAdapter / RequestDevice) and
 * the ProcessEvents busy-wait (preserving the browser Emscripten yield) behind
 * typed stages, replacing the scattered std::atomic<bool> flags and
 * waitForAdapter/waitForDevice helpers in Graphics.cpp.
 */
class InitFlow {
public:
    /** @brief Create a wgpu Instance (native requests the TimedWaitAny feature). */
    static InstanceDone createInstance();

    /**
     * @brief Blocking adapter request. Drives ProcessEvents until the callback
     * fires; throws eve::graphics::Exception when no adapter is found.
     * @param prev  the instance stage (only obtainable from createInstance()).
     * @param compatibleSurface  surface to prefer (may be null for headless).
     */
    static AdapterDone requestAdapter(InstanceDone &&prev, const wgpu::Surface &compatibleSurface);

    /**
     * @brief Blocking device request (plus queue + capability capture).
     * Throws eve::graphics::Exception on failure.
     * @param prev Adapter stage consumed by the request.
     * @param colorAttachmentByteBudget Maximum requested attachment bytes per sample;
     *        32 exercises the WebGPU baseline, while 40 enables the five-target GBuffer.
     * @cost Blocking adapter device creation; perform once per Graphics initialization.
     * @thread Call on the graphics initialization thread; callback work is driven here.
     */
    static DeviceDone requestDevice(AdapterDone&& prev, uint32_t colorAttachmentByteBudget = 40);
};

}  // namespace eve::graphics::webgpu
