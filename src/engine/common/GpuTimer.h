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
    virtual ~IGpuTimer() = default;

    /** @brief True when the renderer captured a valid GPU frame this frame. */
    virtual bool gpuTimingAvailable() const = 0;
    /** @brief GPU execution time of the last presented frame, in milliseconds. */
    virtual float gpuFrameMs() const = 0;
};

}  // namespace eve::service
