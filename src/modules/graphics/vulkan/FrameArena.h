#pragma once

#include "vkbuilder.hpp"

#include <vector>

namespace eve::graphics::vulkan {

/**
 * @brief Per-frame GPU allocation arena (host-visible coherent).
 *
 * One arena per swapchain frame slot. `reset()` at frame start; `alloc()`
 * hands out monotonically increasing offsets until capacity. There is no
 * free/realloc inside a frame: everything written here lives until the slot
 * wraps around, which is exactly the lifetime the frame's command buffer
 * needs.
 */
class FrameArena {
public:
    /** @brief Releases FrameArena resources. */
    ~FrameArena();

    FrameArena(const FrameArena &) = delete;
    FrameArena &operator=(const FrameArena &) = delete;
    /** @brief Constructs a FrameArena. */
    FrameArena() = default;
    /** @brief Constructs a FrameArena. */
    FrameArena(FrameArena &&o) noexcept { *this = std::move(o); }
    /** @brief Operator =. */
    FrameArena &operator=(FrameArena &&o) noexcept;

    /** @brief Alloc public API. */
    struct Alloc {
        vk::DeviceSize offset = 0;
        vk::DeviceSize size = 0;
        void *mapped = nullptr;
    };

    /** @brief Allocate or grow (before any recording) so `bytes` fit. */
    bool ensure(vkb::Device &device, vk::DeviceSize bytes, vk::BufferUsageFlags usage);

    /** @brief Allocate `bytes` (aligned). Returns empty Alloc on capacity overflow. */
    Alloc alloc(vk::DeviceSize bytes, vk::DeviceSize align = 16);

    void reset();

    vk::Buffer buffer() const;
    vk::DeviceSize capacity() const { return capacity_; }
    vk::DeviceSize used() const { return head_; }

private:
    vkb::GenericBuffer buf_;
    vkb::Device device_;
    vk::DeviceSize capacity_ = 0;
    vk::DeviceSize head_ = 0;
    void *mapped_ = nullptr;
};

}  // namespace eve::graphics::vulkan
