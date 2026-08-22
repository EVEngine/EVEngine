#include "graphics/vulkan/FrameGraphJobs.h"

#include "thread/JobSystem.h"

#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace eve::graphics::vulkan {

vkb::PassRecordExecutor jobSystemPassExecutor(eve::thread::JobSystem *jobs) {
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

void recordFrameGraphWithJobSystem(vkb::FrameGraph &graph, eve::thread::JobSystem *jobs) {
    graph.record(jobSystemPassExecutor(jobs));
}

}  // namespace eve::graphics::vulkan
