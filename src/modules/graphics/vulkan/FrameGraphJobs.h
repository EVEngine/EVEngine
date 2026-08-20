#pragma once

#include "thread/JobSystem.h"
#include "vkbuilder/framegraph.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace eve::graphics::vulkan {

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
inline vkb::PassRecordExecutor jobSystemPassExecutor(eve::thread::JobSystem *jobs) {
    return [jobs](const std::vector<uint32_t> &layerPasses,
                  const std::function<void(uint32_t)> &recordOne) {
        if (!jobs || layerPasses.size() <= 1) {
            for (uint32_t order : layerPasses) recordOne(order);
            return;
        }
        auto *group = jobs->createFrameTaskGroup();
        for (uint32_t order : layerPasses)
            group->fork([recordOne, order] { recordOne(order); });
        // Join without help-execution: TaskGroup::wait() help-executes on the
        // waiting thread, and that path races with the arena lifecycle in
        // beginFrame/endFrame (a help-run job's completion can observe
        // outstandingFrame == 0 and reset the per-frame arena while workers
        // are still finishing other jobs). Polling the group's pending count
        // keeps workers as the sole executors; every forked job still drains.
        // (Follow-up: fix the JobSystem help-execution race upstream so this
        // can return to a blocking join.)
        while (group->getPendingCount() > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        // Arena-owned group; never deleted.
    };
}

/**
 * @brief Record a vkb::FrameGraph with the engine JobSystem as the parallel
 * recording executor.
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
 */
inline void recordFrameGraphWithJobSystem(vkb::FrameGraph &graph, eve::thread::JobSystem *jobs) {
    graph.record(jobSystemPassExecutor(jobs));
}

}  // namespace eve::graphics::vulkan
