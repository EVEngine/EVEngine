#include "graphics/vulkan/FrameArena.h"
#include <chrono>
#include <functional>
#include <thread>
#include <vector>
#include "graphics/vulkan/FrameGraphJobs.h"
#include "thread/JobSystem.h"

// FrameArena.cpp
namespace eve::graphics::vulkan {

FrameArena::~FrameArena() {
    // GenericBuffer releases its memory; unmap the persistent mapping first.
    if (mapped_ && device_.instance) {
        buf_.unmap();
    }
    mapped_ = nullptr;
}

FrameArena &FrameArena::operator=(FrameArena &&o) noexcept {
    if (this != &o) {
        if (mapped_ && device_.instance) buf_.unmap();
        mapped_     = o.mapped_;
        o.mapped_   = nullptr;
        device_     = o.device_;
        buf_        = std::move(o.buf_);
        capacity_   = o.capacity_;
        head_       = o.head_;
        o.capacity_ = 0;
        o.head_     = 0;
    }
    return *this;
}

bool FrameArena::ensure(vkb::Device &device, vk::DeviceSize bytes, vk::BufferUsageFlags usage) {
    if (capacity_ >= bytes) return true;
    if (!device.instance) return false;

    const vk::DeviceSize grow   = (capacity_ > 0) ? capacity_ : 1;
    vk::DeviceSize       target = bytes;
    while (target < bytes) target += grow;  // grow at least by current capacity
    target = std::max<vk::DeviceSize>(target, 1 << 20);

    if (mapped_ && device_.instance) buf_.unmap();
    mapped_ = nullptr;
    vkb::GenericBuffer next(device, usage, target,
                            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    buf_      = std::move(next);
    device_   = device;
    capacity_ = target;
    head_     = 0;
    mapped_   = buf_.map();
    return true;
}

FrameArena::Alloc FrameArena::alloc(vk::DeviceSize bytes, vk::DeviceSize align) {
    Alloc out{};
    if (bytes == 0) return out;
    const vk::DeviceSize aligned = ((head_ + align - 1) / align) * align;
    if (aligned + bytes > capacity_) return out;  // overflow: caller truncates + counts
    head_      = aligned + bytes;
    out.offset = aligned;
    out.size   = bytes;
    out.mapped = mapped_ ? static_cast<char *>(mapped_) + aligned : nullptr;
    return out;
}

void FrameArena::reset() { head_ = 0; }

vk::Buffer FrameArena::buffer() const { return buf_.buffer; }

}  // namespace eve::graphics::vulkan

// FrameGraphJobs.cpp
namespace eve::graphics::vulkan {

vkb::PassRecordExecutor jobSystemPassExecutor(eve::thread::JobSystem *jobs) {
    return [jobs](const std::vector<uint32_t> &layerPasses, const std::function<void(uint32_t)> &recordOne) {
        if (!jobs || layerPasses.size() <= 1) {
            for (uint32_t order : layerPasses) recordOne(order);
            return;
        }
        auto *group = jobs->createFrameTaskGroup();
        for (uint32_t order : layerPasses) group->fork([recordOne, order] { recordOne(order); });
        // Join without help-execution: TaskGroup::wait() help-executes on the
        // waiting thread, and that path races with the arena lifecycle in
        // beginFrame/endFrame (a help-run job's completion can observe
        // outstandingFrame == 0 and reset the per-frame arena while workers
        // are still finishing other jobs). Polling the group's pending count
        // keeps workers as the sole executors; every forked job still drains.
        // (Follow-up: fix the JobSystem help-execution race upstream so this
        // can return to a blocking join.)
        while (group->getPendingCount() > 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
        // Arena-owned group; never deleted.
    };
}

void recordFrameGraphWithJobSystem(vkb::FrameGraph &graph, eve::thread::JobSystem *jobs) {
    graph.record(jobSystemPassExecutor(jobs));
}

}  // namespace eve::graphics::vulkan
