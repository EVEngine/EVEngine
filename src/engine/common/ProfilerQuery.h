#pragma once

#include "common/Export.h"

#include <string>

namespace eve {

/**
 * @brief Runtime profiler read surface (provided by the profiler module).
 *
 * The profiler already aggregates a per-module / per-zone call tree every frame,
 * but only Squirrel and the ImGui panel could read it; an agent had no way to ask
 * "where did this frame go", so unattended performance work fell back to guessing
 * from wall-clock timings. This capability exposes the same read-only view.
 *
 * Implementations must only read: finalizing a frame is the frame loop's job, so
 * a query never calls the aggregation step.
 */
class EVENGINE_API IProfilerQuery {
public:
    static constexpr const char* capabilityName = "IProfilerQuery";

    virtual ~IProfilerQuery() = default;

    /** @brief Whether the profiler is currently collecting zones. */
    [[nodiscard]] virtual bool enabled() const = 0;

    /**
     * @brief Last completed frame as compact JSON.
     *
     * Schema `eve.profiler.frame` version 1:
     * `{"schema":…,"version":1,"enabled":…,"hasFrame":…,"cpuFrameMs":…,"gpuMs":…,
     *   "gpuTimingAvailable":…,"zoneCount":…,
     *   "zones":[{"module":…,"name":…,"thread":…,"selfMs":…,"totalMs":…,"count":…,"depth":…}]}`
     *
     * `cpuFrameMs` is the sum of top-level zone self times: the profiler core
     * aggregates zones and does not own the frame loop's wall-clock duration, so
     * this is the CPU work the zones account for, not the presented frame time.
     * `hasFrame` is false before the first completed frame, and `zones` is empty
     * rather than absent.
     */
    [[nodiscard]] virtual std::string frameJson() const = 0;

    /** @brief Human-readable per-zone report of the last completed frame. */
    [[nodiscard]] virtual std::string textReport() const = 0;
};

}  // namespace eve
