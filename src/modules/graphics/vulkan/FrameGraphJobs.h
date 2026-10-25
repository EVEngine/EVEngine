#pragma once
#include "common/Export.h"


#include "vkbuilder.hpp"
#include "vkbuilder/framegraph.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace eve::thread {
class JobSystem;
}

namespace eve::graphics::vulkan {

/** @brief Render-thread-owned recording cost history; reset when graph topology changes.
 * @ownership Stored by Graphics; no callbacks or borrowed job handles are retained. */
struct FrameGraphRecordingState {
    std::chrono::nanoseconds serialCost{};
    uint32_t                 parallelFrames = 0;

    /** @brief Whether the measured graph justifies worker scheduling this frame.
     * Periodically remeasure serial cost so a shrinking workload returns to inline recording. */
    bool isParallelWorthwhile() const { return serialCost >= std::chrono::microseconds(500) && parallelFrames < 60; }
};

/**
 * @brief Build a vkb::PassRecordExecutor backed by the engine JobSystem.
 *
 * The returned executor records every pass in a layer concurrently (one
 * frame-scoped job per pass), joins the layer, then continues with the next
 * layer. Exposed separately so the executor contract can be unit-tested
 * without a Vulkan device.
 *
 * @param jobs Engine JobSystem; must have an active beginFrame()/endFrame()
 *             bracket. May be null, in which case recording is serial.
 */
EVENGINE_API_BACKENDS vkb::PassRecordExecutor jobSystemPassExecutor(eve::thread::JobSystem *jobs);

/**
 * @brief Record a vkb::FrameGraph with the engine JobSystem as the parallel
 * recording executor, with measured-cost selection for small graphs.
 *
 * vkb::FrameGraph groups independent passes into layers; passes inside one
 * layer share no dependency edge, so their command buffers can be recorded
 * concurrently. This executor forks one frame-scoped job per pass in the
 * layer, joins them, then moves to the next layer (layers stay sequential —
 * that is the framegraph's threading contract).
 *
 * The caller must keep a JobSystem frame bracket open (beginFrame() before,
 * endFrame() after) so the fork/join groups come from the per-frame arena and
 * recording allocates no job control blocks per frame.
 *
 * @param graph  Compiled FrameGraph whose passes should be recorded.
 * @param jobs   Engine JobSystem; must have an active beginFrame()/endFrame()
 *               bracket. May be null, in which case recording is serial.
 * @param state Render-thread-owned cost history, reset on graph rebuild; no job handles escape.
 * @cost Recording scales with submitted draws; worker dispatch is amortized only for graphs
 * whose last inline recording exceeded 500 microseconds. Remeasured after 60 parallel frames.
 */
void recordFrameGraphWithJobSystem(vkb::FrameGraph &graph, eve::thread::JobSystem *jobs,
                                   FrameGraphRecordingState &state);

}  // namespace eve::graphics::vulkan
