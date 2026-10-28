#pragma once

#include <cstdint>

// GPU timing capability. The graphics module (LAYER 3) provides a live
// implementation; lower-layer consumers (e.g. the profiler module) query it via
// eve::cap::query<I>() and cope with nullptr (when graphics is absent / disabled).
// This keeps the interface in common/ so layering is not violated.

namespace eve::service {

/**
 * @brief Reads the GPU frame timing captured by the renderer via Vulkan
 *        timestamp queries.
 * Providers: eve::graphics::Graphics.
 */
class IGpuTimer {
public:
    static constexpr const char *capabilityName = "IGpuTimer";
    /** @brief Releases IGpuTimer resources. */
    virtual ~IGpuTimer() = default;

    /** @brief True when the renderer captured a valid GPU frame this frame. */
    virtual bool gpuTimingAvailable() const = 0;
    /** @brief Graphics-queue interval of the latest completed frame slot, in milliseconds.
     * Delayed by frames in flight; includes deferred work and queue idle gaps
     * between submissions, but excludes CPU preparation before the first submission.
     * @thread Render/main thread only. @ownership Returns a copied scalar. */
    virtual float gpuFrameMs() const = 0;
};

}  // namespace eve::service
